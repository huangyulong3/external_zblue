/*
 * Copyright (c) 2024 Xiaomi Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * HAL implementation for NuttX QEMU (Goldfish ARM) with socket-based
 * virtual radio. Uses GIC SGI (Software Generated Interrupts) for
 * interrupt-context execution of BLE Controller ISR handlers.
 *
 * IRQ mapping (GIC SGI, always-enabled on GICv2):
 *   SGI 4 = Radio ISR       (replaces isr_radio / LL_RADIO_IRQn)
 *   SGI 5 = Ticker ISR      (replaces rtc0_rv32m1_isr / LL_RTC0_IRQn)
 *   SGI 6 = SWI LLL         (replaces swi_lll_rv32m1_isr)
 *   SGI 7 = SWI ULL LOW     (replaces swi_ull_low_rv32m1_isr)
 *
 * All handlers run in true interrupt context via up_trigger_irq().
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/sys/byteorder.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/clock.h>

#include "hal/radio_vendor_hal.h"
#include "hal/swi_vendor_hal.h"

#include "util/mem.h"
#include "util/memq.h"
#include "util/mayfly.h"
#include "ticker/ticker.h"
#include "hal/ticker_vendor_hal.h"
#include "hal/ccm.h"
#include "lll.h"

#include "pdu_df.h"
#include "lll/pdu_vendor.h"
#include "pdu.h"

#include "lll_vendor.h"
#include "lll/lll_adv_types.h"
#include "lll_adv.h"
#include "lll/lll_adv_pdu.h"
#include "lll_scan.h"
#include "lll_conn.h"
#include "lll_filter.h"
#include "lll_internal.h"

#include "ull_tx_queue.h"
#include "ull_adv_types.h"
#include "ull_scan_types.h"
#include "ull_conn_types.h"
#include "ull_internal.h"

#include "hal/debug.h"

/* ============================================================
 * SGI ISR Handlers (run in true interrupt context)
 * ============================================================ */

/* Radio ISR context: callback + param set by radio_isr_set() */
static struct {
    radio_isr_cb_t isr_cb;
    void          *isr_cb_param;
} radio_ctx;

static int radio_isr_handler(int irq, void *context, void *arg)
{
    (void)irq;
    (void)context;
    (void)arg;

    if (radio_ctx.isr_cb) {
        radio_ctx.isr_cb(radio_ctx.isr_cb_param);
    }

    return 0;
}

static int ticker_isr_handler(int irq, void *context, void *arg)
{
    (void)irq;
    (void)context;
    (void)arg;

    ticker_trigger(0);

    mayfly_run(TICKER_USER_ID_ULL_HIGH);
    mayfly_run(TICKER_USER_ID_ULL_LOW);

    return 0;
}

static int swi_lll_isr_handler(int irq, void *context, void *arg)
{
    (void)irq;
    (void)context;
    (void)arg;

    mayfly_run(TICKER_USER_ID_LLL);

    return 0;
}

static int swi_ull_low_isr_handler(int irq, void *context, void *arg)
{
    (void)irq;
    (void)context;
    (void)arg;

    mayfly_run(TICKER_USER_ID_ULL_LOW);

    return 0;
}

/* ============================================================
 * SWI Init / Pend (GIC SGI based)
 * ============================================================ */

void hal_swi_init(void)
{
    irq_attach(BT_CTLR_IRQ_RADIO,   radio_isr_handler,       NULL);
    irq_attach(BT_CTLR_IRQ_TICKER,  ticker_isr_handler,      NULL);
    irq_attach(BT_CTLR_IRQ_SWI_LLL, swi_lll_isr_handler,     NULL);
    irq_attach(BT_CTLR_IRQ_SWI_ULL, swi_ull_low_isr_handler, NULL);
}

void hal_swi_lll_pend(void)
{
    up_trigger_irq(BT_CTLR_IRQ_SWI_LLL, 0);
}

void hal_swi_ull_low_pend(void)
{
    up_trigger_irq(BT_CTLR_IRQ_SWI_ULL, 0);
}

void hal_swi_radio_pend(void)
{
    up_trigger_irq(BT_CTLR_IRQ_RADIO, 0);
}

/* ============================================================
 * Radio ISR Set
 * Uses up_irq_save/restore since SGIs cannot be individually disabled
 * ============================================================ */

void radio_isr_set(radio_isr_cb_t cb, void *param)
{
    irqstate_t flags;

    flags = up_irq_save();

    radio_ctx.isr_cb_param = param;
    radio_ctx.isr_cb       = cb;

    up_irq_restore(flags);
}

/* ============================================================
 * Virtual Radio State
 * ============================================================ */

static struct {
    /* Socket for virtual radio */
    int       tx_sock;
    int       rx_sock;
    pthread_t rx_thread;
    bool      rx_running;

    /* Radio configuration */
    uint8_t   phy;
    uint8_t   phy_flags;
    int8_t    tx_power;
    uint32_t  freq_chan;
    uint32_t  whiten_iv;
    uint8_t   access_addr[4];
    uint32_t  crc_poly;
    uint32_t  crc_iv;

    /* Packet buffers */
    void     *tx_pkt;
    void     *rx_pkt;
    uint8_t   scratch[256];

    /* Radio state */
    bool      tx_enabled;
    bool      rx_enabled;
    bool      disabled;
    bool      crc_valid;

    /* Timer state */
    uint32_t  tmr_start;
    uint32_t  tmr_end;
    uint32_t  tmr_aa;
    uint32_t  tmr_ready;
    uint32_t  tifs;
    uint32_t  hcto;

    /* Filter state */
    uint8_t   filter_bitmask;
    uint8_t   filter_addr_type;
    uint8_t   filter_bdaddr[8 * 6];
    bool      filter_match;
    uint32_t  filter_match_idx;

    /* Switch mode */
    uint8_t   switch_mode; /* 0=disable, 1=rx, 2=tx */
    uint8_t   switch_phy;
    uint8_t   switch_flags;
} radio_state;

/* Forward declaration for radio_filter_check (used in rx thread) */
static void radio_filter_check(uint8_t tx_addr, const uint8_t *addr);

/* UDP ports for virtual radio */
#define RADIO_TX_PORT  12340
#define RADIO_RX_PORT  12341
#define RADIO_MCAST_ADDR "239.0.0.1"

/* ============================================================
 * RX Thread - receives UDP packets and triggers Radio ISR via SGI
 * ============================================================ */

static void *radio_rx_thread(void *arg)
{
    struct pollfd pfd;
    uint8_t buf[256];
    int ret;

    (void)arg;

    pfd.fd = radio_state.rx_sock;
    pfd.events = POLLIN;

    while (radio_state.rx_running) {
        ret = poll(&pfd, 1, 100); /* 100ms timeout */
        if (ret > 0 && (pfd.revents & POLLIN)) {
            ret = recv(radio_state.rx_sock, buf, sizeof(buf), 0);
            if (ret > 0 && radio_state.rx_pkt && radio_state.rx_enabled) {
                memcpy(radio_state.rx_pkt, buf, ret);
                radio_state.crc_valid = true;

                /* Run address filter against received PDU.
                 * BLE advertising PDU: byte[0] has type/TxAdd/RxAdd,
                 * advertiser address starts at byte[2].
                 * TxAdd is bit 6 of byte[0].
                 */
                if (ret >= 8) {
                    uint8_t tx_addr = (buf[0] >> 6) & 1;
                    radio_filter_check(tx_addr, &buf[2]);
                }

                radio_state.rx_enabled = false;
                radio_state.disabled = true;

                /* Trigger Radio ISR via SGI */
                up_trigger_irq(BT_CTLR_IRQ_RADIO, 0);
            }
        }
    }

    return NULL;
}

/* ============================================================
 * Socket-based Virtual Radio Init/Deinit
 * ============================================================ */

int radio_sim_init(void)
{
    struct sockaddr_in addr;
    struct ip_mreq mreq;
    int opt = 1;

    /* Create TX socket (UDP unicast/multicast) */
    radio_state.tx_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (radio_state.tx_sock < 0) {
        return -errno;
    }

    setsockopt(radio_state.tx_sock, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));

    /* Create RX socket (UDP multicast) */
    radio_state.rx_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (radio_state.rx_sock < 0) {
        close(radio_state.tx_sock);
        return -errno;
    }

    setsockopt(radio_state.rx_sock, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(RADIO_RX_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(radio_state.rx_sock, (struct sockaddr *)&addr,
             sizeof(addr)) < 0) {
        close(radio_state.tx_sock);
        close(radio_state.rx_sock);
        return -errno;
    }

    /* Join multicast group */
    mreq.imr_multiaddr.s_addr = inet_addr(RADIO_MCAST_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(radio_state.rx_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
               &mreq, sizeof(mreq));

    /* Set RX socket non-blocking */
    fcntl(radio_state.rx_sock, F_SETFL, O_NONBLOCK);

    /* Start RX thread */
    radio_state.rx_running = true;
    pthread_create(&radio_state.rx_thread, NULL, radio_rx_thread, NULL);

    return 0;
}

void radio_sim_deinit(void)
{
    radio_state.rx_running = false;
    pthread_join(radio_state.rx_thread, NULL);

    if (radio_state.tx_sock >= 0) {
        close(radio_state.tx_sock);
        radio_state.tx_sock = -1;
    }

    if (radio_state.rx_sock >= 0) {
        close(radio_state.rx_sock);
        radio_state.rx_sock = -1;
    }
}

int radio_sim_send(const void *data, uint16_t len)
{
    struct sockaddr_in dest;

    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(RADIO_RX_PORT);
    dest.sin_addr.s_addr = inet_addr(RADIO_MCAST_ADDR);

    return sendto(radio_state.tx_sock, data, len, 0,
                  (struct sockaddr *)&dest, sizeof(dest));
}

int radio_sim_recv(void *data, uint16_t max_len)
{
    return recv(radio_state.rx_sock, data, max_len, MSG_DONTWAIT);
}

/* ============================================================
 * Radio Setup / Reset / Configuration
 * ============================================================ */

void radio_setup(void)
{
    memset(&radio_state, 0, sizeof(radio_state));
    radio_state.tx_sock = -1;
    radio_state.rx_sock = -1;
    radio_state.crc_valid = true;
    radio_state.disabled = true;

    radio_sim_init();
}

void radio_reset(void)
{
    radio_state.tx_enabled = false;
    radio_state.rx_enabled = false;
    radio_state.disabled = true;
    radio_state.crc_valid = true;
    radio_state.filter_match = false;
    radio_state.switch_mode = 0;
}

void radio_phy_set(uint8_t phy, uint8_t flags)
{
    radio_state.phy = phy;
    radio_state.phy_flags = flags;
}

void radio_tx_power_set(int8_t power)
{
    radio_state.tx_power = power;
}

void radio_freq_chan_set(uint32_t chan)
{
    radio_state.freq_chan = chan;
}

void radio_whiten_iv_set(uint32_t iv)
{
    radio_state.whiten_iv = iv;
}

void radio_aa_set(const uint8_t *aa)
{
    memcpy(radio_state.access_addr, aa, 4);
}

void radio_pkt_configure(uint8_t bits_len, uint8_t max_len, uint8_t flags)
{
    (void)bits_len;
    (void)max_len;
    (void)flags;
}

void radio_pkt_rx_set(void *rx_packet)
{
    radio_state.rx_pkt = rx_packet;
}

void radio_pkt_tx_set(void *tx_packet)
{
    radio_state.tx_pkt = tx_packet;
}

uint32_t radio_tx_ready_delay_get(uint8_t phy, uint8_t flags)
{
    (void)phy;
    (void)flags;
    return 40; /* 40us simulated TX ramp-up */
}

uint32_t radio_rx_ready_delay_get(uint8_t phy, uint8_t flags)
{
    (void)phy;
    (void)flags;
    return 40; /* 40us simulated RX ramp-up */
}

uint32_t radio_is_ready(void)
{
    return 1;
}

uint32_t radio_is_done(void)
{
    return radio_state.disabled;
}

uint32_t radio_has_disabled(void)
{
    return radio_state.disabled;
}

uint32_t radio_is_idle(void)
{
    return radio_state.disabled &&
           !radio_state.tx_enabled &&
           !radio_state.rx_enabled;
}

void radio_crc_configure(uint32_t polynomial, uint32_t iv)
{
    radio_state.crc_poly = polynomial;
    radio_state.crc_iv = iv;
}

uint32_t radio_crc_is_valid(void)
{
    return radio_state.crc_valid ? 1 : 0;
}

/* ============================================================
 * Radio TX / RX Enable / Disable
 * ============================================================ */

void radio_rx_enable(void)
{
    radio_state.rx_enabled = true;
    radio_state.tx_enabled = false;
    radio_state.disabled = false;
}

void radio_tx_enable(void)
{
    radio_state.tx_enabled = true;
    radio_state.rx_enabled = false;
    radio_state.disabled = false;

    /* Send TX packet via socket */
    if (radio_state.tx_pkt) {
        /* PDU header (2 bytes) + payload length from header byte[1] */
        uint8_t *pdu = (uint8_t *)radio_state.tx_pkt;
        uint16_t len = 2 + pdu[1];
        radio_sim_send(pdu, len);
    }

    radio_state.tx_enabled = false;
    radio_state.disabled = true;

    /* Trigger Radio ISR via SGI to signal TX done */
    up_trigger_irq(BT_CTLR_IRQ_RADIO, 0);
}

void radio_disable(void)
{
    radio_state.tx_enabled = false;
    radio_state.rx_enabled = false;
    radio_state.disabled = true;
}

void radio_status_reset(void)
{
    /* Nothing to reset in simulation */
}

void radio_tmr_status_reset(void)
{
    /* Nothing to reset in simulation */
}

void radio_rssi_status_reset(void)
{
    /* Nothing to reset in simulation */
}

void radio_rssi_measure(void)
{
    /* Stub */
}

uint32_t radio_rssi_is_ready(void)
{
    return 1;
}

uint32_t radio_rssi_get(void)
{
    return 60; /* -60 dBm simulated RSSI */
}

uint32_t radio_is_tx_done(void)
{
    return radio_state.disabled && !radio_state.tx_enabled;
}

uint32_t radio_phy_flags_rx_get(void)
{
    return 0;
}

void *radio_pkt_scratch_get(void)
{
    return radio_state.scratch;
}

uint32_t radio_tx_chain_delay_get(uint8_t phy, uint8_t flags)
{
    (void)phy;
    (void)flags;
    return 0;
}

uint32_t radio_rx_chain_delay_get(uint8_t phy, uint8_t flags)
{
    (void)phy;
    (void)flags;
    return 0;
}

/* ============================================================
 * Radio Switch Functions
 * ============================================================ */

void radio_switch_complete_and_rx(uint8_t phy)
{
    radio_state.switch_mode = 1; /* switch to RX after TX */
    radio_state.switch_phy = phy;
}

void radio_switch_complete_and_tx(uint8_t phy, uint8_t flags_rx,
                                  uint8_t phy_tx, uint8_t flags_tx)
{
    (void)flags_rx;
    (void)phy_tx;
    (void)flags_tx;
    radio_state.switch_mode = 2; /* switch to TX after RX */
    radio_state.switch_phy = phy;
}

void radio_switch_complete_and_disable(void)
{
    radio_state.switch_mode = 0; /* disable after operation */
}

void radio_switch_complete_and_b2b_tx(uint8_t phy_curr, uint8_t flags_curr,
                                      uint8_t phy_next, uint8_t flags_next)
{
    (void)phy_curr;
    (void)flags_curr;
    (void)phy_next;
    (void)flags_next;
    radio_state.switch_mode = 2;
}

void radio_switch_complete_and_b2b_tx_disable(void)
{
    radio_state.switch_mode = 0;
}

/* ============================================================
 * Radio Timer Functions
 * Uses NuttX clock for timing simulation
 * ============================================================ */

static uint32_t get_us_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
}

uint32_t radio_tmr_start(uint8_t trx, uint32_t ticks_start,
                         uint32_t remainder)
{
    (void)trx;
    (void)ticks_start;
    (void)remainder;

    radio_state.tmr_start = get_us_time();
    radio_state.tmr_ready = radio_state.tmr_start + 40;

    return radio_state.tmr_start;
}

void radio_tmr_stop(void)
{
    /* Nothing to stop in simulation */
}

void radio_tmr_hcto_configure(uint32_t hcto)
{
    radio_state.hcto = hcto;
}

void radio_tmr_tifs_set(uint32_t tifs)
{
    radio_state.tifs = tifs;
}

uint32_t radio_tmr_aa_get(void)
{
    return radio_state.tmr_aa;
}

uint32_t radio_tmr_aa_restore(void)
{
    return radio_state.tmr_aa;
}

uint32_t radio_tmr_ready_get(void)
{
    return radio_state.tmr_ready;
}

uint32_t radio_tmr_end_get(void)
{
    return radio_state.tmr_end;
}

void radio_tmr_end_capture(void)
{
    radio_state.tmr_end = get_us_time();
}

void radio_tmr_sample(void)
{
    /* Stub */
}

uint32_t radio_tmr_sample_get(void)
{
    return get_us_time();
}

uint32_t radio_tmr_start_get(void)
{
    return radio_state.tmr_start;
}

uint32_t radio_tmr_tifs_base_get(void)
{
    return radio_state.tmr_start;
}

/* ============================================================
 * Radio Filter Functions — Real whitelist address filtering
 *
 * radio_filter_configure() stores up to 8 filter entries.
 * radio_filter_check() is called from the RX path to match
 * a received PDU's advertiser address against the filter list.
 * ============================================================ */

#define RADIO_FILTER_MAX_ENTRIES 8
#define BDADDR_SIZE 6

void radio_filter_configure(uint8_t bitmask_enable, uint8_t bitmask_addr_type,
                            uint8_t *bdaddr)
{
    radio_state.filter_bitmask = bitmask_enable;
    radio_state.filter_addr_type = bitmask_addr_type;
    if (bdaddr) {
        memcpy(radio_state.filter_bdaddr, bdaddr,
               RADIO_FILTER_MAX_ENTRIES * BDADDR_SIZE);
    }
    radio_state.filter_match = false;
    radio_state.filter_match_idx = 0;
}

void radio_filter_disable(void)
{
    radio_state.filter_bitmask = 0;
    radio_state.filter_match = false;
    radio_state.filter_match_idx = 0;
}

void radio_filter_status_reset(void)
{
    radio_state.filter_match = false;
    radio_state.filter_match_idx = 0;
}

/**
 * radio_filter_check - Check a received PDU address against the filter list.
 *
 * @param tx_addr  Address type from PDU header (0=public, 1=random)
 * @param addr     Pointer to 6-byte address from the PDU
 *
 * Call this from the RX path after receiving a PDU. It sets
 * filter_match / filter_match_idx which are read by the LLL ISR
 * via radio_filter_has_match() / radio_filter_match_get().
 */
static void radio_filter_check(uint8_t tx_addr, const uint8_t *addr)
{
    uint8_t i;

    radio_state.filter_match = false;
    radio_state.filter_match_idx = 0;

    if (!radio_state.filter_bitmask) {
        return;
    }

    for (i = 0; i < RADIO_FILTER_MAX_ENTRIES; i++) {
        if (!(radio_state.filter_bitmask & BIT(i))) {
            continue;
        }

        /* Check address type matches */
        uint8_t entry_addr_type = (radio_state.filter_addr_type >> i) & 1;
        if (entry_addr_type != tx_addr) {
            continue;
        }

        /* Compare 6-byte BD_ADDR */
        if (memcmp(&radio_state.filter_bdaddr[i * BDADDR_SIZE],
                   addr, BDADDR_SIZE) == 0) {
            radio_state.filter_match = true;
            radio_state.filter_match_idx = i;
            return;
        }
    }
}

uint32_t radio_filter_has_match(void)
{
    return radio_state.filter_match ? 1 : 0;
}

uint32_t radio_filter_match_get(void)
{
    return radio_state.filter_match_idx;
}

/* ============================================================
 * GPIO PA/LNA Stubs
 * ============================================================ */

void radio_gpio_pa_setup(void)
{
}

void radio_gpio_lna_setup(void)
{
}

void radio_gpio_pa_lna_enable(uint32_t trx_us)
{
    (void)trx_us;
}

/* ============================================================
 * Address Resolution Stubs
 * ============================================================ */

void radio_ar_configure(uint32_t count, uint8_t *irks, uint8_t flags)
{
    (void)count;
    (void)irks;
    (void)flags;
}

uint32_t radio_ar_has_match(void)
{
    return 0;
}

uint32_t radio_ar_match_get(void)
{
    return 0;
}

void radio_ar_status_reset(void)
{
}

/* ============================================================
 * Ticker HAL - POSIX timer based
 * Simulates the RTC0 hardware timer for the BLE ticker.
 * Timer fires periodically and triggers Ticker ISR via SGI.
 * ============================================================ */

static timer_t ticker_timer_id;
static bool    ticker_timer_created;

static void ticker_timer_handler(union sigval sv)
{
    (void)sv;

    /* Trigger Ticker ISR via SGI - runs in interrupt context */
    up_trigger_irq(BT_CTLR_IRQ_TICKER, 0);
}

uint32_t cntr_start(void)
{
    struct sigevent sev;
    struct itimerspec its;

    if (ticker_timer_created) {
        return 0;
    }

    memset(&sev, 0, sizeof(sev));
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = ticker_timer_handler;

    if (timer_create(CLOCK_MONOTONIC, &sev, &ticker_timer_id) < 0) {
        return 1;
    }

    ticker_timer_created = true;

    /* Start periodic timer: ~30.5us per tick (32768 Hz) */
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 30518; /* ~1 tick at 32768 Hz */
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 30518;

    if (timer_settime(ticker_timer_id, 0, &its, NULL) < 0) {
        timer_delete(ticker_timer_id);
        ticker_timer_created = false;
        return 1;
    }

    return 0;
}

uint32_t cntr_stop(void)
{
    if (ticker_timer_created) {
        timer_delete(ticker_timer_id);
        ticker_timer_created = false;
    }

    return 0;
}

static uint32_t counter_val;

uint32_t cntr_cnt_get(void)
{
    /* Return a monotonically increasing counter based on real time.
     * Simulates a 32768 Hz counter (24-bit).
     */
    struct timespec ts;
    uint64_t us;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    us = (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;

    /* Convert microseconds to 32768 Hz ticks */
    return (uint32_t)((us * 32768ULL) / 1000000ULL) & HAL_TICKER_CNTR_MASK;
}

uint32_t cntr_cmp_set(uint8_t cmp, uint32_t value)
{
    (void)cmp;
    (void)value;
    return 0;
}

/* ============================================================
 * Mayfly Pend - triggers SWI via SGI
 * ============================================================ */

void mayfly_pend(uint8_t caller_id, uint8_t callee_id)
{
    (void)caller_id;

    switch (callee_id) {
    case TICKER_USER_ID_LLL:
        up_trigger_irq(BT_CTLR_IRQ_SWI_LLL, 0);
        break;

    case TICKER_USER_ID_ULL_HIGH:
        /* ULL_HIGH runs in ticker ISR context */
        up_trigger_irq(BT_CTLR_IRQ_TICKER, 0);
        break;

    case TICKER_USER_ID_ULL_LOW:
        up_trigger_irq(BT_CTLR_IRQ_SWI_ULL, 0);
        break;

    default:
        break;
    }
}

/* ============================================================
 * Mayfly helper functions required by mayfly.c
 * ============================================================ */

void mayfly_enable_cb(uint8_t caller_id, uint8_t callee_id, uint8_t enable)
{
    (void)caller_id;
    (void)callee_id;
    (void)enable;
}

uint32_t mayfly_is_enabled(uint8_t caller_id, uint8_t callee_id)
{
    (void)caller_id;
    (void)callee_id;
    return 1; /* Always enabled */
}

uint32_t mayfly_prio_is_equal(uint8_t caller_id, uint8_t callee_id)
{
    (void)caller_id;
    (void)callee_id;
    return (caller_id == callee_id) ? 1 : 0;
}

uint32_t mayfly_is_running(void)
{
    return 0;
}

/* ============================================================
 * Ticker update (required by ull_adv.c, ull_peripheral.c)
 * ============================================================ */

uint8_t ticker_update(uint8_t instance_index, uint8_t user_id,
                      uint8_t ticker_id, uint32_t ticks_drift_plus,
                      uint32_t ticks_drift_minus, uint32_t ticks_slot_plus,
                      uint32_t ticks_slot_minus, uint16_t lazy,
                      uint8_t force,
                      ticker_op_func fp_op_func, void *op_context)
{
    (void)instance_index;
    (void)user_id;
    (void)ticker_id;
    (void)ticks_drift_plus;
    (void)ticks_drift_minus;
    (void)ticks_slot_plus;
    (void)ticks_slot_minus;
    (void)lazy;
    (void)force;

    if (fp_op_func) {
        fp_op_func(TICKER_STATUS_SUCCESS, op_context);
    }

    return TICKER_STATUS_SUCCESS;
}

/* ============================================================
 * HCI recv FIFO reset (called from hci.c reset())
 * hci_driver.c is not compiled, so we provide a no-op stub.
 * ============================================================ */

void hci_recv_fifo_reset(void)
{
    /* No-op: QEMU virtual radio does not use HCI recv FIFO */
}

/* ============================================================
 * LLL Advertising Implementation
 *
 * Simplified for QEMU virtual radio:
 * - No GPIO PA/LNA
 * - No ISR profiling
 * - No XTAL_ADVANCED
 * - Uses port's lll_adv_pdu.h inline accessors
 * ============================================================ */

static int adv_prepare_cb(struct lll_prepare_param *prepare_param);
static int adv_is_abort_cb(void *next, void *curr,
                           lll_prepare_cb_t *resume_cb);
static void adv_abort_cb(struct lll_prepare_param *prepare_param,
                         void *param);
static void adv_isr_tx(void *param);
static void adv_isr_rx(void *param);
static void adv_isr_done(void *param);
static void adv_isr_abort(void *param);
static void adv_isr_cleanup(void *param);
static void adv_isr_race(void *param);
static void adv_chan_prepare(struct lll_adv *lll);

static inline int adv_isr_rx_pdu(struct lll_adv *lll,
                                 uint8_t devmatch_ok, uint8_t devmatch_id,
                                 uint8_t irkmatch_ok, uint8_t irkmatch_id,
                                 uint8_t rssi_ready);
static inline bool adv_isr_rx_sr_check(struct lll_adv *lll,
                                       struct pdu_adv *adv,
                                       struct pdu_adv *sr,
                                       uint8_t devmatch_ok,
                                       uint8_t *rl_idx);
static inline bool adv_isr_rx_sr_adva_check(struct pdu_adv *adv,
                                            struct pdu_adv *sr);

void lll_adv_prepare(void *param)
{
    struct lll_prepare_param *p = param;
    int err;

    err = lll_clk_on();
    LL_ASSERT(!err || err == -EINPROGRESS);

    err = lll_prepare(adv_is_abort_cb, adv_abort_cb, adv_prepare_cb, 0, p);
    LL_ASSERT(!err || err == -EINPROGRESS);
}

static int adv_prepare_cb(struct lll_prepare_param *prepare_param)
{
    struct lll_adv *lll = prepare_param->param;
    uint32_t aa = sys_cpu_to_le32(PDU_AC_ACCESS_ADDR);
    uint32_t ticks_at_event, ticks_at_start;
    uint32_t remainder_us;
    struct ull_hdr *ull;
    uint32_t remainder;

    DEBUG_RADIO_START_A(1);

#if defined(CONFIG_BT_PERIPHERAL)
    /* Check if stopped (on connection establishment race) */
    if (unlikely(lll->conn && lll->conn->central.initiated)) {
        int err;

        err = lll_clk_off();
        LL_ASSERT(!err || err == -EBUSY);

        lll_done(NULL);

        DEBUG_RADIO_START_A(0);
        return 0;
    }
#endif /* CONFIG_BT_PERIPHERAL */

    radio_reset();
    radio_tx_power_set(RADIO_TXP_DEFAULT);

#if defined(CONFIG_BT_CTLR_ADV_EXT)
    radio_phy_set(lll->phy_p, 1);
    radio_pkt_configure(8, PDU_AC_LEG_PAYLOAD_SIZE_MAX, (lll->phy_p << 1));
#else
    radio_phy_set(0, 0);
    radio_pkt_configure(8, PDU_AC_LEG_PAYLOAD_SIZE_MAX, 0);
#endif

    radio_aa_set((uint8_t *)&aa);
    radio_crc_configure(((0x5bUL) | ((0x06UL) << 8) | ((0x00UL) << 16)),
                        0x555555);

    lll->chan_map_curr = lll->chan_map;

    adv_chan_prepare(lll);

#if defined(CONFIG_BT_CTLR_PRIVACY)
    if (ull_filter_lll_rl_enabled()) {
        struct lll_filter *filter =
            ull_filter_lll_get(!!(lll->filter_policy));

        radio_filter_configure(filter->enable_bitmask,
                               filter->addr_type_bitmask,
                               (uint8_t *)filter->bdaddr);
    } else
#endif /* CONFIG_BT_CTLR_PRIVACY */
    if (IS_ENABLED(CONFIG_BT_CTLR_FILTER_ACCEPT_LIST) &&
        lll->filter_policy) {
        struct lll_filter *fal = ull_filter_lll_get(true);

        radio_filter_configure(fal->enable_bitmask,
                               fal->addr_type_bitmask,
                               (uint8_t *)fal->bdaddr);
    }

    ticks_at_event = prepare_param->ticks_at_expire;
    ull = HDR_LLL2ULL(lll);
    ticks_at_event += lll_event_offset_get(ull);

    ticks_at_start = ticks_at_event;
    ticks_at_start += HAL_TICKER_US_TO_TICKS(EVENT_OVERHEAD_START_US);

    remainder = prepare_param->remainder;
    remainder_us = radio_tmr_start(1, ticks_at_start, remainder);

    /* capture end of Tx-ed PDU, used to calculate HCTO */
    radio_tmr_end_capture();

    (void)remainder_us;

    {
        uint32_t ret;

        ret = lll_prepare_done(lll);
        LL_ASSERT(!ret);
    }

    DEBUG_RADIO_START_A(1);

    return 0;
}

static int adv_is_abort_cb(void *next, void *curr, lll_prepare_cb_t *resume_cb)
{
    /* TODO: prio check */
    if (next != curr) {
#if defined(CONFIG_BT_PERIPHERAL)
        struct lll_adv *lll = curr;

        if (lll->is_hdcd) {
            int err;

            *resume_cb = adv_prepare_cb;

            err = lll_clk_on();
            LL_ASSERT(!err || err == -EINPROGRESS);

            return -EAGAIN;
        }
#endif /* CONFIG_BT_PERIPHERAL */
        return -ECANCELED;
    }

#if defined(CONFIG_BT_PERIPHERAL)
    {
        struct lll_adv *lll = curr;
        struct pdu_adv *pdu = lll_adv_data_curr_get(lll);

        if (pdu->type == PDU_ADV_TYPE_DIRECT_IND) {
            return 0;
        }
    }
#endif /* CONFIG_BT_PERIPHERAL */

    return -ECANCELED;
}

static void adv_abort_cb(struct lll_prepare_param *prepare_param, void *param)
{
    int err;

    if (!prepare_param) {
        radio_isr_set(adv_isr_abort, param);
        radio_disable();
        return;
    }

    err = lll_clk_off();
    LL_ASSERT(!err || err == -EBUSY);

    lll_done(param);
}

static void adv_isr_tx(void *param)
{
    uint32_t hcto;

    /* Clear radio status and events */
    radio_status_reset();
    radio_tmr_status_reset();

    /* setup tIFS switching */
    radio_tmr_tifs_set(EVENT_IFS_US);
    radio_switch_complete_and_tx(0, 0, 0, 0);

    radio_pkt_rx_set(radio_pkt_scratch_get());
    LL_ASSERT(!radio_is_ready());

    radio_isr_set(adv_isr_rx, param);

#if defined(CONFIG_BT_CTLR_PRIVACY)
    if (ull_filter_lll_rl_enabled()) {
        uint8_t count, *irks = ull_filter_lll_irks_get(&count);

        radio_ar_configure(count, irks, 0U);
    }
#endif /* CONFIG_BT_CTLR_PRIVACY */

    /* +/- 2us active clock jitter, +1 us hcto compensation */
    hcto = radio_tmr_tifs_base_get() + EVENT_IFS_US + 4 + 1;
    hcto += radio_rx_chain_delay_get(0, 0);
    hcto += 40; /* addr_us_get(0) approximation */
    hcto -= radio_tx_chain_delay_get(0, 0);
    radio_tmr_hcto_configure(hcto);

    /* capture end of CONNECT_IND PDU */
    radio_tmr_end_capture();

    if (IS_ENABLED(CONFIG_BT_CTLR_SCAN_REQ_RSSI) ||
        IS_ENABLED(CONFIG_BT_CTLR_CONN_RSSI)) {
        radio_rssi_measure();
    }
}

static void adv_isr_rx(void *param)
{
    uint8_t trx_done;
    uint8_t crc_ok;
    uint8_t devmatch_ok;
    uint8_t devmatch_id;
    uint8_t irkmatch_ok;
    uint8_t irkmatch_id;
    uint8_t rssi_ready;

    /* Read radio status and events */
    trx_done = radio_is_done();
    if (trx_done) {
        crc_ok = radio_crc_is_valid();
        devmatch_ok = radio_filter_has_match();
        devmatch_id = radio_filter_match_get();
        if (IS_ENABLED(CONFIG_BT_CTLR_PRIVACY)) {
            irkmatch_ok = radio_ar_has_match();
            irkmatch_id = radio_ar_match_get();
        } else {
            irkmatch_ok = 0U;
            irkmatch_id = FILTER_IDX_NONE;
        }
        rssi_ready = radio_rssi_is_ready();
    } else {
        crc_ok = devmatch_ok = irkmatch_ok = rssi_ready = 0U;
        devmatch_id = irkmatch_id = FILTER_IDX_NONE;
    }

    /* Clear radio status and events */
    lll_isr_status_reset();

    if (!trx_done) {
        goto isr_rx_do_close;
    }

    if (crc_ok) {
        int err;

        err = adv_isr_rx_pdu(param, devmatch_ok, devmatch_id,
                             irkmatch_ok, irkmatch_id, rssi_ready);
        if (!err) {
            return;
        }
    }

isr_rx_do_close:
    radio_isr_set(adv_isr_done, param);
    radio_disable();
}

static void adv_isr_done(void *param)
{
    struct lll_adv *lll = param;

    /* Clear radio status and events */
    lll_isr_status_reset();

#if defined(CONFIG_BT_PERIPHERAL)
    if (!lll->chan_map_curr && lll->is_hdcd) {
        lll->chan_map_curr = lll->chan_map;
    }
#endif /* CONFIG_BT_PERIPHERAL */

    if (lll->chan_map_curr) {
        adv_chan_prepare(lll);

        /* Start TX on next advertising channel */
        radio_tx_enable();

        /* capture end of Tx-ed PDU, used to calculate HCTO */
        radio_tmr_end_capture();

        return;
    }

    radio_filter_disable();

    adv_isr_cleanup(param);
}

static void adv_isr_abort(void *param)
{
    lll_isr_status_reset();
    radio_filter_disable();
    adv_isr_cleanup(param);
}

static void adv_isr_cleanup(void *param)
{
    int err;

    radio_isr_set(adv_isr_race, param);
    radio_tmr_stop();

    err = lll_clk_off();
    LL_ASSERT(!err || err == -EBUSY);

    lll_done(NULL);
}

static void adv_isr_race(void *param)
{
    (void)param;
    radio_status_reset();
}

static void adv_chan_prepare(struct lll_adv *lll)
{
    struct pdu_adv *pdu;
    struct pdu_adv *scan_pdu;
    uint8_t chan;

    pdu = lll_adv_data_curr_get(lll);
    LL_ASSERT(pdu);
    scan_pdu = lll_adv_scan_rsp_peek(lll);
    LL_ASSERT(scan_pdu);

    radio_pkt_tx_set(pdu);

    if ((pdu->type != PDU_ADV_TYPE_NONCONN_IND) &&
        (!IS_ENABLED(CONFIG_BT_CTLR_ADV_EXT) ||
         (pdu->type != PDU_ADV_TYPE_EXT_IND))) {
        radio_isr_set(adv_isr_tx, lll);
        radio_tmr_tifs_set(EVENT_IFS_US);
        radio_switch_complete_and_rx(0);
    } else {
        radio_isr_set(adv_isr_done, lll);
        radio_switch_complete_and_disable();
    }

    chan = __builtin_ffs(lll->chan_map_curr);
    LL_ASSERT(chan);

    lll->chan_map_curr &= (lll->chan_map_curr - 1);

    lll_chan_set(36 + chan);
}

static inline int adv_isr_rx_pdu(struct lll_adv *lll,
                                 uint8_t devmatch_ok, uint8_t devmatch_id,
                                 uint8_t irkmatch_ok, uint8_t irkmatch_id,
                                 uint8_t rssi_ready)
{
    struct pdu_adv *pdu_rx, *pdu_adv;

#if defined(CONFIG_BT_CTLR_PRIVACY)
    uint8_t rl_idx = irkmatch_ok ?
        ull_filter_lll_rl_irk_idx(irkmatch_id) : FILTER_IDX_NONE;
#else
    uint8_t rl_idx = FILTER_IDX_NONE;
#endif

    pdu_rx = (void *)radio_pkt_scratch_get();
    pdu_adv = lll_adv_data_curr_get(lll);

    if ((pdu_rx->type == PDU_ADV_TYPE_SCAN_REQ) &&
        (pdu_rx->len == sizeof(struct pdu_adv_scan_req)) &&
        (pdu_adv->type != PDU_ADV_TYPE_DIRECT_IND) &&
        adv_isr_rx_sr_check(lll, pdu_adv, pdu_rx, devmatch_ok, &rl_idx)) {
        radio_isr_set(adv_isr_done, lll);
        radio_switch_complete_and_disable();
        radio_pkt_tx_set(lll_adv_scan_rsp_peek(lll));

        LL_ASSERT(!radio_is_ready());

        return 0;

#if defined(CONFIG_BT_PERIPHERAL)
    } else if ((pdu_rx->type == PDU_ADV_TYPE_CONNECT_IND) &&
               (pdu_rx->len == sizeof(struct pdu_adv_connect_ind)) &&
               lll->conn) {
        struct node_rx_ftr *ftr;
        struct node_rx_pdu *rx;

        if (IS_ENABLED(CONFIG_BT_CTLR_CHAN_SEL_2)) {
            rx = ull_pdu_rx_alloc_peek(4);
        } else {
            rx = ull_pdu_rx_alloc_peek(3);
        }

        if (!rx) {
            return -ENOBUFS;
        }

        radio_isr_set(adv_isr_abort, lll);
        radio_disable();

        LL_ASSERT(!radio_is_ready());

#if defined(CONFIG_BT_CTLR_CONN_RSSI)
        if (rssi_ready) {
            lll->conn->rssi_latest = radio_rssi_get();
        }
#endif /* CONFIG_BT_CTLR_CONN_RSSI */

        /* Stop further LLL radio events */
        lll->conn->central.initiated = 1;

        rx = ull_pdu_rx_alloc();

        rx->hdr.type = NODE_RX_TYPE_CONNECTION;
        rx->hdr.handle = 0xffff;

        memcpy(rx->pdu, pdu_rx,
               (offsetof(struct pdu_adv, connect_ind) +
                sizeof(struct pdu_adv_connect_ind)));

        ftr = &(rx->rx_ftr);
        ftr->param = lll;
        ftr->ticks_anchor = radio_tmr_start_get();
        ftr->radio_end_us = radio_tmr_end_get() -
                            radio_tx_chain_delay_get(0, 0);

#if defined(CONFIG_BT_CTLR_PRIVACY)
        ftr->rl_idx = irkmatch_ok ? rl_idx : FILTER_IDX_NONE;
#endif /* CONFIG_BT_CTLR_PRIVACY */

        if (IS_ENABLED(CONFIG_BT_CTLR_CHAN_SEL_2)) {
            ftr->extra = ull_pdu_rx_alloc();
        }

        ull_rx_put_sched(rx->hdr.link, rx);

        return 0;
#endif /* CONFIG_BT_PERIPHERAL */
    }

    return -EINVAL;
}

static inline bool adv_isr_rx_sr_check(struct lll_adv *lll,
                                       struct pdu_adv *adv,
                                       struct pdu_adv *sr,
                                       uint8_t devmatch_ok,
                                       uint8_t *rl_idx)
{
#if defined(CONFIG_BT_CTLR_PRIVACY)
    return ((((lll->filter_policy & BT_LE_ADV_FP_FILTER_SCAN_REQ) == 0) &&
             ull_filter_lll_rl_addr_allowed(sr->tx_addr,
                                            sr->scan_req.scan_addr,
                                            rl_idx)) ||
            (((lll->filter_policy & BT_LE_ADV_FP_FILTER_SCAN_REQ) != 0) &&
             (devmatch_ok || ull_filter_lll_irk_in_fal(*rl_idx)))) &&
           adv_isr_rx_sr_adva_check(adv, sr);
#else
    return (((lll->filter_policy & BT_LE_ADV_FP_FILTER_SCAN_REQ) == 0U) ||
            devmatch_ok) &&
           adv_isr_rx_sr_adva_check(adv, sr);
#endif /* CONFIG_BT_CTLR_PRIVACY */
}

static inline bool adv_isr_rx_sr_adva_check(struct pdu_adv *adv,
                                            struct pdu_adv *sr)
{
    return (adv->tx_addr == sr->rx_addr) &&
           !memcmp(adv->adv_ind.addr, sr->scan_req.adv_addr, BDADDR_SIZE);
}

/* ============================================================
 * LLL Scanning Implementation
 *
 * Simplified for QEMU virtual radio:
 * - No GPIO PA/LNA
 * - No ISR profiling
 * - No XTAL_ADVANCED
 * ============================================================ */

static int scan_prepare_cb(struct lll_prepare_param *prepare_param);
static int scan_is_abort_cb(void *next, void *curr,
                            lll_prepare_cb_t *resume_cb);
static void scan_abort_cb(struct lll_prepare_param *prepare_param,
                          void *param);
static void scan_ticker_stop_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
                                uint32_t remainder, uint16_t lazy,
                                uint8_t force, void *param);
static void scan_ticker_op_start_cb(uint32_t status, void *param);
static void scan_isr_rx(void *param);
static void scan_isr_tx(void *param);
static void scan_isr_done(void *param);
static void scan_isr_window(void *param);
static void scan_isr_abort(void *param);
static void scan_isr_cleanup(void *param);
static void scan_isr_race(void *param);

static inline bool scan_isr_rx_check(struct lll_scan *lll,
                                     uint8_t irkmatch_ok,
                                     uint8_t devmatch_ok,
                                     uint8_t rl_idx);
static inline uint32_t scan_isr_rx_pdu(struct lll_scan *lll,
                                       uint8_t devmatch_ok,
                                       uint8_t devmatch_id,
                                       uint8_t irkmatch_ok,
                                       uint8_t irkmatch_id,
                                       uint8_t rl_idx,
                                       uint8_t rssi_ready);
static inline bool scan_isr_scan_rsp_adva_matches(struct pdu_adv *srsp);
static uint32_t scan_isr_rx_report(struct lll_scan *lll, uint8_t rssi_ready,
                                   uint8_t rl_idx, bool dir_report);

/* ============================================================
 * LLL Connection stubs (central/peripheral/conn_flush)
 * These are referenced by ull_central.c, ull_peripheral.c,
 * ull_conn.c. Provide minimal stubs for QEMU.
 * ============================================================ */

void lll_central_prepare(void *param)
{
    /* TODO: implement full central connection event for QEMU */
    (void)param;
}

void lll_periph_prepare(void *param)
{
    /* TODO: implement full peripheral connection event for QEMU */
    (void)param;
}

void lll_conn_flush(uint16_t handle, struct lll_conn *lll)
{
    (void)handle;
    (void)lll;
    /* Nothing to be flushed in QEMU virtual radio */
}

static inline bool scan_isr_tgta_check(struct lll_scan *lll, bool init,
                                       struct pdu_adv *pdu, uint8_t rl_idx,
                                       bool *dir_report);
static inline bool scan_isr_tgta_rpa_check(struct lll_scan *lll,
                                           struct pdu_adv *pdu,
                                           bool *dir_report);

void lll_scan_prepare(void *param)
{
    struct lll_prepare_param *p = param;
    int err;

    err = lll_clk_on();
    LL_ASSERT(!err || err == -EINPROGRESS);

    err = lll_prepare(scan_is_abort_cb, scan_abort_cb, scan_prepare_cb,
                      0, p);
    LL_ASSERT(!err || err == -EINPROGRESS);
}

static int scan_prepare_cb(struct lll_prepare_param *prepare_param)
{
    struct lll_scan *lll = prepare_param->param;
    uint32_t aa = sys_cpu_to_le32(PDU_AC_ACCESS_ADDR);
    uint32_t ticks_at_event, ticks_at_start;
    struct node_rx_pdu *node_rx;
    uint32_t remainder_us;
    struct ull_hdr *ull;
    uint32_t remainder;

    DEBUG_RADIO_START_O(1);

#if defined(CONFIG_BT_CENTRAL)
    /* Check if stopped (on connection establishment race) */
    if (unlikely(lll->conn && lll->conn->central.initiated)) {
        int err;

        err = lll_clk_off();
        LL_ASSERT(!err || err == -EBUSY);

        lll_done(NULL);

        DEBUG_RADIO_START_O(0);
        return 0;
    }
#endif /* CONFIG_BT_CENTRAL */

    radio_reset();
    radio_tx_power_set(RADIO_TXP_DEFAULT);

#if defined(CONFIG_BT_CTLR_ADV_EXT)
    radio_phy_set(lll->phy, 1);
    radio_pkt_configure(8, PDU_AC_LEG_PAYLOAD_SIZE_MAX, (lll->phy << 1));
#else
    radio_phy_set(0, 0);
    radio_pkt_configure(8, PDU_AC_LEG_PAYLOAD_SIZE_MAX, 0);
#endif

    node_rx = ull_pdu_rx_alloc_peek(1);
    LL_ASSERT(node_rx);
    radio_pkt_rx_set(node_rx->pdu);

    radio_aa_set((uint8_t *)&aa);
    radio_crc_configure(((0x5bUL) | ((0x06UL) << 8) | ((0x00UL) << 16)),
                        0x555555);

    lll_chan_set(37 + lll->chan);

    radio_isr_set(scan_isr_rx, lll);

    /* setup tIFS switching */
    radio_tmr_tifs_set(EVENT_IFS_US);
    radio_switch_complete_and_tx(0, 0, 0, 0);

#if defined(CONFIG_BT_CTLR_PRIVACY)
    if (ull_filter_lll_rl_enabled()) {
        struct lll_filter *filter =
            ull_filter_lll_get((lll->filter_policy &
                                SCAN_FP_FILTER) != 0U);
        uint8_t count, *irks = ull_filter_lll_irks_get(&count);

        radio_filter_configure(filter->enable_bitmask,
                               filter->addr_type_bitmask,
                               (uint8_t *)filter->bdaddr);

        radio_ar_configure(count, irks, 0U);
    } else
#endif /* CONFIG_BT_CTLR_PRIVACY */
    if (IS_ENABLED(CONFIG_BT_CTLR_FILTER_ACCEPT_LIST) &&
        lll->filter_policy) {
        struct lll_filter *fal = ull_filter_lll_get(true);

        radio_filter_configure(fal->enable_bitmask,
                               fal->addr_type_bitmask,
                               (uint8_t *)fal->bdaddr);
    }

    ticks_at_event = prepare_param->ticks_at_expire;
    ull = HDR_LLL2ULL(lll);
    ticks_at_event += lll_event_offset_get(ull);

    ticks_at_start = ticks_at_event;
    ticks_at_start += HAL_TICKER_US_TO_TICKS(EVENT_OVERHEAD_START_US);

    remainder = prepare_param->remainder;
    remainder_us = radio_tmr_start(0, ticks_at_start, remainder);

    /* capture end of Rx-ed PDU */
    radio_tmr_end_capture();

    /* scanner always measures RSSI */
    radio_rssi_measure();

    (void)remainder_us;

    {
        uint32_t ret;

        if (lll->ticks_window) {
            /* start window close timeout */
            ret = ticker_start(TICKER_INSTANCE_ID_CTLR,
                               TICKER_USER_ID_LLL,
                               TICKER_ID_SCAN_STOP,
                               ticks_at_event, lll->ticks_window,
                               TICKER_NULL_PERIOD,
                               TICKER_NULL_REMAINDER,
                               TICKER_NULL_LAZY, TICKER_NULL_SLOT,
                               scan_ticker_stop_cb, lll,
                               scan_ticker_op_start_cb,
                               (void *)(uintptr_t)__LINE__);
            LL_ASSERT((ret == TICKER_STATUS_SUCCESS) ||
                      (ret == TICKER_STATUS_BUSY));
        }

        ret = lll_prepare_done(lll);
        LL_ASSERT(!ret);
    }

    DEBUG_RADIO_START_O(1);

    return 0;
}

static int scan_resume_prepare_cb(struct lll_prepare_param *p)
{
    struct ull_hdr *ull = HDR_LLL2ULL(p->param);

    p->ticks_at_expire = ticker_ticks_now_get() - lll_event_offset_get(ull);
    p->remainder = 0;
    p->lazy = 0;

    return scan_prepare_cb(p);
}

static int scan_is_abort_cb(void *next, void *curr,
                            lll_prepare_cb_t *resume_cb)
{
    struct lll_scan *lll = curr;

    if (next != curr) {
        int err;

        /* wrap back after the pre-empter */
        *resume_cb = scan_resume_prepare_cb;

        err = lll_clk_on();
        LL_ASSERT(!err || err == -EINPROGRESS);

        return -EAGAIN;
    }

    radio_isr_set(scan_isr_window, lll);
    radio_disable();

    if (++lll->chan == 3U) {
        lll->chan = 0U;
    }

    lll_chan_set(37 + lll->chan);

    return 0;
}

static void scan_abort_cb(struct lll_prepare_param *prepare_param,
                          void *param)
{
    int err;

    if (!prepare_param) {
        radio_isr_set(scan_isr_abort, param);
        radio_disable();
        return;
    }

    err = lll_clk_off();
    LL_ASSERT(!err || err == -EBUSY);

    lll_done(param);
}

static void scan_ticker_stop_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
                                uint32_t remainder, uint16_t lazy,
                                uint8_t force, void *param)
{
    (void)ticks_at_expire;
    (void)ticks_drift;
    (void)remainder;
    (void)lazy;
    (void)force;

    radio_isr_set(scan_isr_cleanup, param);
    radio_disable();
}

static void scan_ticker_op_start_cb(uint32_t status, void *param)
{
    (void)param;
    LL_ASSERT(status == TICKER_STATUS_SUCCESS);
}

static void scan_isr_rx(void *param)
{
    struct lll_scan *lll = (void *)param;
    uint8_t trx_done;
    uint8_t crc_ok;
    uint8_t devmatch_ok;
    uint8_t devmatch_id;
    uint8_t irkmatch_ok;
    uint8_t irkmatch_id;
    uint8_t rssi_ready;
    uint8_t rl_idx;

    /* Read radio status and events */
    trx_done = radio_is_done();
    if (trx_done) {
        crc_ok = radio_crc_is_valid();
        devmatch_ok = radio_filter_has_match();
        devmatch_id = radio_filter_match_get();
        if (IS_ENABLED(CONFIG_BT_CTLR_PRIVACY)) {
            irkmatch_ok = radio_ar_has_match();
            irkmatch_id = radio_ar_match_get();
        } else {
            irkmatch_ok = 0U;
            irkmatch_id = FILTER_IDX_NONE;
        }
        rssi_ready = radio_rssi_is_ready();
    } else {
        crc_ok = devmatch_ok = irkmatch_ok = rssi_ready = 0U;
        devmatch_id = irkmatch_id = FILTER_IDX_NONE;
    }

    /* Clear radio status and events */
    lll_isr_status_reset();

    if (!trx_done) {
        goto isr_rx_do_close;
    }

#if defined(CONFIG_BT_CTLR_PRIVACY)
    rl_idx = devmatch_ok ?
             ull_filter_lll_rl_idx(((lll->filter_policy &
                                     SCAN_FP_FILTER) != 0U),
                                   devmatch_id) :
             irkmatch_ok ? ull_filter_lll_rl_irk_idx(irkmatch_id) :
                           FILTER_IDX_NONE;
#else
    rl_idx = FILTER_IDX_NONE;
#endif

    if (crc_ok && scan_isr_rx_check(lll, irkmatch_ok, devmatch_ok,
                                    rl_idx)) {
        uint32_t err;

        err = scan_isr_rx_pdu(lll, devmatch_ok, devmatch_id,
                              irkmatch_ok, irkmatch_id, rl_idx,
                              rssi_ready);
        if (!err) {
            return;
        }
    }

isr_rx_do_close:
    radio_isr_set(scan_isr_done, lll);
    radio_disable();
}

static void scan_isr_tx(void *param)
{
    struct node_rx_pdu *node_rx;

    /* Clear radio status and events */
    radio_status_reset();
    radio_tmr_status_reset();

    /* setup tIFS switching */
    radio_tmr_tifs_set(EVENT_IFS_US);
    radio_switch_complete_and_tx(0, 0, 0, 0);

    node_rx = ull_pdu_rx_alloc_peek(1);
    LL_ASSERT(node_rx);
    radio_pkt_rx_set(node_rx->pdu);

    LL_ASSERT(!radio_is_ready());

#if defined(CONFIG_BT_CTLR_PRIVACY)
    if (ull_filter_lll_rl_enabled()) {
        uint8_t count, *irks = ull_filter_lll_irks_get(&count);

        radio_ar_configure(count, irks, 0);
    }
#endif /* CONFIG_BT_CTLR_PRIVACY */

    radio_rssi_measure();

    radio_isr_set(scan_isr_rx, param);
}

static void scan_isr_common_done(void *param)
{
    struct node_rx_pdu *node_rx;

    /* Clear radio status and events */
    lll_isr_status_reset();

    /* setup tIFS switching */
    radio_tmr_tifs_set(EVENT_IFS_US);
    radio_switch_complete_and_tx(0, 0, 0, 0);

    node_rx = ull_pdu_rx_alloc_peek(1);
    LL_ASSERT(node_rx);
    radio_pkt_rx_set(node_rx->pdu);

#if defined(CONFIG_BT_CTLR_PRIVACY)
    if (ull_filter_lll_rl_enabled()) {
        uint8_t count, *irks = ull_filter_lll_irks_get(&count);

        radio_ar_configure(count, irks, 0);
    }
#endif /* CONFIG_BT_CTLR_PRIVACY */

    radio_rssi_measure();

    radio_isr_set(scan_isr_rx, param);
}

static void scan_isr_done(void *param)
{
    scan_isr_common_done(param);

    /* Re-enable RX for next advertising packet */
    radio_rx_enable();

    /* capture end of Rx-ed PDU */
    radio_tmr_end_capture();
}

static void scan_isr_window(void *param)
{
    scan_isr_common_done(param);

    /* capture end of Rx-ed PDU */
    radio_tmr_end_capture();
}

static void scan_isr_abort(void *param)
{
    lll_isr_status_reset();

    /* Scanner stop can expire while here in this ISR.
     * Deferred attempt to stop can fail as it would have
     * expired, hence ignore failure.
     */
    (void)ticker_stop(TICKER_INSTANCE_ID_CTLR, TICKER_USER_ID_LLL,
                      TICKER_ID_SCAN_STOP, NULL, NULL);

    radio_disable();

    scan_isr_cleanup(param);
}

static void scan_isr_cleanup(void *param)
{
    struct lll_scan *lll = param;

    if (lll_is_done(param, NULL)) {
        return;
    }

    radio_filter_disable();

    if (++lll->chan == 3U) {
        lll->chan = 0U;
    }

    radio_isr_set(scan_isr_race, param);
    radio_tmr_stop();

    {
        int err;

        err = lll_clk_off();
        LL_ASSERT(!err || err == -EBUSY);
    }

    lll_done(NULL);
}

static void scan_isr_race(void *param)
{
    (void)param;
    radio_status_reset();
}

static inline bool scan_isr_rx_check(struct lll_scan *lll,
                                     uint8_t irkmatch_ok,
                                     uint8_t devmatch_ok,
                                     uint8_t rl_idx)
{
#if defined(CONFIG_BT_CTLR_PRIVACY)
    return (((lll->filter_policy & SCAN_FP_FILTER) == 0U) &&
            (!devmatch_ok ||
             ull_filter_lll_rl_idx_allowed(irkmatch_ok, rl_idx))) ||
           (((lll->filter_policy & SCAN_FP_FILTER) != 0U) &&
            (devmatch_ok || ull_filter_lll_irk_in_fal(rl_idx)));
#else
    return ((lll->filter_policy & SCAN_FP_FILTER) == 0U) ||
           devmatch_ok;
#endif /* CONFIG_BT_CTLR_PRIVACY */
}

static inline uint32_t scan_isr_rx_pdu(struct lll_scan *lll,
                                       uint8_t devmatch_ok,
                                       uint8_t devmatch_id,
                                       uint8_t irkmatch_ok,
                                       uint8_t irkmatch_id,
                                       uint8_t rl_idx,
                                       uint8_t rssi_ready)
{
    struct node_rx_pdu *node_rx;
    struct pdu_adv *pdu_adv_rx;
    bool dir_report = false;

    node_rx = ull_pdu_rx_alloc_peek(1);
    LL_ASSERT(node_rx);

    pdu_adv_rx = (void *)node_rx->pdu;

    if (0) {
#if defined(CONFIG_BT_CENTRAL)
    /* Initiator */
    } else if (lll->conn) {
        /* Simplified: initiator connection logic would go here.
         * For QEMU, we skip the full initiator path for now.
         */
        return 1;
#endif /* CONFIG_BT_CENTRAL */

    /* Active scanner */
    } else if (((pdu_adv_rx->type == PDU_ADV_TYPE_ADV_IND) ||
                (pdu_adv_rx->type == PDU_ADV_TYPE_SCAN_IND)) &&
               (pdu_adv_rx->len <= sizeof(struct pdu_adv_adv_ind)) &&
               lll->type &&
#if defined(CONFIG_BT_CENTRAL)
               !lll->conn) {
#else
               1) {
#endif
        struct pdu_adv *pdu_tx;
        uint32_t err;

        /* setup tIFS switching */
        radio_tmr_tifs_set(EVENT_IFS_US);
        radio_switch_complete_and_rx(0);

        /* save the adv packet */
        err = scan_isr_rx_report(lll, rssi_ready,
                                 irkmatch_ok ? rl_idx : FILTER_IDX_NONE,
                                 false);
        if (err) {
            return err;
        }

        /* prepare the scan request packet */
        pdu_tx = (void *)radio_pkt_scratch_get();
        pdu_tx->type = PDU_ADV_TYPE_SCAN_REQ;
        pdu_tx->rx_addr = pdu_adv_rx->tx_addr;
        pdu_tx->len = sizeof(struct pdu_adv_scan_req);

#if defined(CONFIG_BT_CTLR_PRIVACY)
        {
            bt_addr_t *lrpa = ull_filter_lll_lrpa_get(rl_idx);

            if (lll->rpa_gen && lrpa) {
                pdu_tx->tx_addr = 1;
                memcpy(&pdu_tx->scan_req.scan_addr[0], lrpa->val,
                       BDADDR_SIZE);
            } else {
                pdu_tx->tx_addr = lll->init_addr_type;
                memcpy(&pdu_tx->scan_req.scan_addr[0],
                       &lll->init_addr[0], BDADDR_SIZE);
            }
        }
#else
        pdu_tx->tx_addr = lll->init_addr_type;
        memcpy(&pdu_tx->scan_req.scan_addr[0],
               &lll->init_addr[0], BDADDR_SIZE);
#endif /* CONFIG_BT_CTLR_PRIVACY */

        memcpy(&pdu_tx->scan_req.adv_addr[0],
               &pdu_adv_rx->adv_ind.addr[0], BDADDR_SIZE);

        radio_pkt_tx_set(pdu_tx);

        LL_ASSERT(!radio_is_ready());

        /* capture end of Tx-ed PDU, used to calculate HCTO */
        radio_tmr_end_capture();

        /* switch scanner state to active */
        lll->state = 1U;
        radio_isr_set(scan_isr_tx, lll);

        return 0;
    }
    /* Passive scanner or scan responses */
    else if (((((pdu_adv_rx->type == PDU_ADV_TYPE_ADV_IND) ||
                (pdu_adv_rx->type == PDU_ADV_TYPE_NONCONN_IND) ||
                (pdu_adv_rx->type == PDU_ADV_TYPE_SCAN_IND)) &&
               (pdu_adv_rx->len <= sizeof(struct pdu_adv_adv_ind))) ||
              ((pdu_adv_rx->type == PDU_ADV_TYPE_DIRECT_IND) &&
               (pdu_adv_rx->len == sizeof(struct pdu_adv_direct_ind)) &&
               (scan_isr_tgta_check(lll, false, pdu_adv_rx, rl_idx,
                                    &dir_report))) ||
              ((pdu_adv_rx->type == PDU_ADV_TYPE_SCAN_RSP) &&
               (pdu_adv_rx->len <= sizeof(struct pdu_adv_scan_rsp)) &&
               (lll->state != 0U) &&
               scan_isr_scan_rsp_adva_matches(pdu_adv_rx))) &&
             (pdu_adv_rx->len != 0) &&
#if defined(CONFIG_BT_CENTRAL)
             !lll->conn) {
#else
             1) {
#endif
        uint32_t err;

        /* save the scan response packet */
        err = scan_isr_rx_report(lll, rssi_ready,
                                 irkmatch_ok ? rl_idx : FILTER_IDX_NONE,
                                 dir_report);
        if (err) {
            return err;
        }
    }
    /* invalid PDU */
    else {
        return 1;
    }

    return 1;
}

static inline bool scan_isr_tgta_check(struct lll_scan *lll, bool init,
                                       struct pdu_adv *pdu, uint8_t rl_idx,
                                       bool *dir_report)
{
#if defined(CONFIG_BT_CTLR_PRIVACY)
    if (ull_filter_lll_rl_addr_resolve(pdu->rx_addr,
                                       pdu->direct_ind.tgt_addr, rl_idx)) {
        return true;
    } else if (init && lll->rpa_gen &&
               ull_filter_lll_lrpa_get(rl_idx)) {
        return false;
    }
#endif /* CONFIG_BT_CTLR_PRIVACY */

    return (((lll->init_addr_type == pdu->rx_addr) &&
             !memcmp(lll->init_addr, pdu->direct_ind.tgt_addr,
                     BDADDR_SIZE))) ||
           scan_isr_tgta_rpa_check(lll, pdu, dir_report);
}

static inline bool scan_isr_tgta_rpa_check(struct lll_scan *lll,
                                           struct pdu_adv *pdu,
                                           bool *dir_report)
{
    if (((lll->filter_policy & SCAN_FP_EXT) != 0U) &&
        (pdu->rx_addr != 0) &&
        ((pdu->direct_ind.tgt_addr[5] & 0xc0) == 0x40)) {
        if (dir_report) {
            *dir_report = true;
        }
        return true;
    }

    return false;
}

static inline bool scan_isr_scan_rsp_adva_matches(struct pdu_adv *srsp)
{
    struct pdu_adv *sreq = (void *)radio_pkt_scratch_get();

    return ((sreq->rx_addr == srsp->tx_addr) &&
            (memcmp(&sreq->scan_req.adv_addr[0],
                    &srsp->scan_rsp.addr[0], BDADDR_SIZE) == 0));
}

static uint32_t scan_isr_rx_report(struct lll_scan *lll, uint8_t rssi_ready,
                                   uint8_t rl_idx, bool dir_report)
{
    struct node_rx_pdu *node_rx;

    node_rx = ull_pdu_rx_alloc_peek(3);
    if (!node_rx) {
        return 1;
    }
    ull_pdu_rx_alloc();

    /* Prepare the report (adv or scan resp) */
    node_rx->hdr.handle = 0xffff;

#if defined(CONFIG_BT_CTLR_ADV_EXT)
    if (lll->phy) {
        switch (lll->phy) {
        case BIT(0):
            node_rx->hdr.type = NODE_RX_TYPE_EXT_1M_REPORT;
            break;
        case BIT(2):
            node_rx->hdr.type = NODE_RX_TYPE_EXT_CODED_REPORT;
            break;
        default:
            LL_ASSERT(0);
            break;
        }
    } else
#endif /* CONFIG_BT_CTLR_ADV_EXT */
    {
        node_rx->hdr.type = NODE_RX_TYPE_REPORT;
    }

    node_rx->rx_ftr.rssi = (rssi_ready) ?
                            (radio_rssi_get() & 0x7f) : 0x7f;
#if defined(CONFIG_BT_CTLR_PRIVACY)
    node_rx->rx_ftr.rl_idx = rl_idx;
#endif /* CONFIG_BT_CTLR_PRIVACY */
#if defined(CONFIG_BT_CTLR_EXT_SCAN_FP)
    node_rx->rx_ftr.direct = dir_report;
#endif /* CONFIG_BT_CTLR_EXT_SCAN_FP */

    ull_rx_put_sched(node_rx->hdr.link, node_rx);

    return 0;
}
