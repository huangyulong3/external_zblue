/*
 * Copyright (c) 2024 Xiaomi Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * LLL (Lower Link Layer) implementation for NuttX QEMU (Goldfish ARM) port.
 * Replaces hardware-dependent LLL functions with GIC SGI based
 * software interrupts and socket-based radio simulation.
 *
 * Reference: openisa lll.c implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/sys/byteorder.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "hal/swi.h"
#include "hal/ccm.h"
#include "hal/radio.h"
#include "hal/ticker.h"

#include "util/util.h"
#include "util/mem.h"
#include "util/memq.h"
#include "util/mayfly.h"

#include "ticker/ticker.h"

#include "pdu_df.h"
#include "lll/pdu_vendor.h"
#include "pdu.h"

#include "lll.h"
#include "lll_vendor.h"
#include "lll/lll_adv_types.h"
#include "lll_adv.h"
#include "lll/lll_adv_pdu.h"
#include "lll_scan.h"
#include "lll_conn.h"
#include "lll_internal.h"

#include "ull_tx_queue.h"
#include "ull_adv_types.h"
#include "ull_scan_types.h"
#include "ull_conn_types.h"
#include "ull_internal.h"

#include "hal/debug.h"

/* ============================================================
 * LLL Event State
 * ============================================================ */

static struct {
    struct {
        void              *param;
        lll_is_abort_cb_t is_abort_cb;
        lll_abort_cb_t    abort_cb;
    } curr;

#if defined(CONFIG_BT_CTLR_LOW_LAT_ULL_DONE)
    struct {
        uint8_t volatile lll_count;
        uint8_t          ull_count;
    } done;
#endif /* CONFIG_BT_CTLR_LOW_LAT_ULL_DONE */
} event;

static int init_reset(void);
#if defined(CONFIG_BT_CTLR_LOW_LAT_ULL_DONE)
static inline void done_inc(void);
#endif /* CONFIG_BT_CTLR_LOW_LAT_ULL_DONE */
static inline bool is_done_sync(void);
static struct lll_event *resume_enqueue(lll_prepare_cb_t resume_cb);

#if !defined(CONFIG_BT_CTLR_LOW_LAT)
static void ticker_stop_op_cb(uint32_t status, void *param);
static void ticker_start_op_cb(uint32_t status, void *param);
static void ticker_start_next_op_cb(uint32_t status, void *param);
static uint32_t preempt_ticker_start(struct lll_event *event,
                                     ticker_op_func op_cb);
static void preempt_ticker_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
                              uint32_t remainder, uint16_t lazy, uint8_t force,
                              void *param);
static void preempt(void *param);
#else /* CONFIG_BT_CTLR_LOW_LAT */
#if (CONFIG_BT_CTLR_LLL_PRIO == CONFIG_BT_CTLR_ULL_LOW_PRIO)
static void mfy_ticker_job_idle_get(void *param);
static void ticker_op_job_disable(uint32_t status, void *op_context);
#endif
#endif /* CONFIG_BT_CTLR_LOW_LAT */

/* ============================================================
 * LLL Initialization
 * Reference: openisa lll.c lll_init()
 * ============================================================ */

int lll_init(void)
{
    int err;

    /* Initialise LLL internals */
    event.curr.abort_cb = NULL;

    err = init_reset();
    if (err) {
        return err;
    }

    /* Initialize SW IRQ structure (work queues) */
    hal_swi_init();

    /*
     * In the openisa implementation, hardware IRQs are connected here:
     *   IRQ_CONNECT(LL_RADIO_IRQn, ..., isr_radio, ...)
     *   IRQ_CONNECT(LL_RTC0_IRQn, ..., rtc0_rv32m1_isr, ...)
     *   IRQ_CONNECT(HAL_SWI_RADIO_IRQ, ..., swi_lll_rv32m1_isr, ...)
     *   IRQ_CONNECT(HAL_SWI_JOB_IRQ, ..., swi_ull_low_rv32m1_isr, ...)
     *
     * In NuttX QEMU (Goldfish ARM / GICv2), we replace these with
     * GIC SGI software interrupts for true interrupt-context execution:
     * - SGI 4: Radio ISR - triggered when socket data arrives or TX done
     * - SGI 5: Ticker ISR - triggered by POSIX timer (hal_radio_qemu.c)
     * - SGI 6: SWI LLL - triggered via hal_swi_lll_pend()
     * - SGI 7: SWI ULL LOW - triggered via hal_swi_ull_low_pend()
     *
     * hal_swi_init() calls irq_attach() for all 4 SGIs.
     * SGIs are always-enabled on GICv2, no irq_enable needed.
     */

    /* Initialize the virtual radio (socket-based) */
    radio_setup();

    return 0;
}

int lll_deinit(void)
{
    radio_sim_deinit();
    return 0;
}

/* ============================================================
 * Random Number Generation
 * ============================================================ */

int lll_csrand_get(void *buf, size_t len)
{
    uint8_t *p = buf;
    while (len--) {
        *p++ = (uint8_t)(rand() & 0xFF);
    }
    return 0;
}

int lll_csrand_isr_get(void *buf, size_t len)
{
    return lll_csrand_get(buf, len);
}

int lll_rand_get(void *buf, size_t len)
{
    return lll_csrand_get(buf, len);
}

int lll_rand_isr_get(void *buf, size_t len)
{
    return lll_csrand_get(buf, len);
}

/* ============================================================
 * LLL Reset
 * ============================================================ */

int lll_reset(void)
{
    int err;

    err = init_reset();
    if (err) {
        return err;
    }

    return 0;
}

/* ============================================================
 * LLL Disable
 * Reference: openisa lll.c lll_disable()
 * ============================================================ */

void lll_disable(void *param)
{
    /* LLL disable of current event, done is generated */
    if (!param || (param == event.curr.param)) {
        if (event.curr.abort_cb && event.curr.param) {
            event.curr.abort_cb(NULL, event.curr.param);
        } else {
            LL_ASSERT(!param);
        }
    }
    {
        struct lll_event *next;
        uint8_t idx;

        idx = UINT8_MAX;
        next = ull_prepare_dequeue_iter(&idx);
        while (next) {
            if (!next->is_aborted &&
                (!param || (param == next->prepare_param.param))) {
                next->is_aborted = 1;
                next->abort_cb(&next->prepare_param,
                               next->prepare_param.param);

#if !defined(CONFIG_BT_CTLR_LOW_LAT_ULL_DONE)
                /* NOTE: abort_cb called lll_done which modifies
                 *       the prepare pipeline hence re-iterate
                 *       through the prepare pipeline.
                 */
                idx = UINT8_MAX;
#endif /* CONFIG_BT_CTLR_LOW_LAT_ULL_DONE */
            }

            next = ull_prepare_dequeue_iter(&idx);
        }
    }
}

/* ============================================================
 * LLL Prepare / Done
 * Reference: openisa lll.c
 * ============================================================ */

int lll_prepare_done(void *param)
{
#if defined(CONFIG_BT_CTLR_LOW_LAT) && \
        (CONFIG_BT_CTLR_LLL_PRIO == CONFIG_BT_CTLR_ULL_LOW_PRIO)
    static memq_link_t link;
    static struct mayfly mfy = {0, 0, &link, NULL, mfy_ticker_job_idle_get};
    uint32_t ret;

    ret = mayfly_enqueue(TICKER_USER_ID_LLL, TICKER_USER_ID_ULL_LOW,
                         1, &mfy);
    if (ret) {
        return -EFAULT;
    }

    return 0;
#else
    return 0;
#endif /* CONFIG_BT_CTLR_LOW_LAT */
}

int lll_done(void *param)
{
    struct lll_event *next;
    struct ull_hdr *ull;
    void *evdone;

    /* Assert if param supplied without a pending prepare to cancel. */
    next = ull_prepare_dequeue_get();
    LL_ASSERT(!param || next);

    /* check if current LLL event is done */
    ull = NULL;
    if (!param) {
        /* Reset current event instance */
        LL_ASSERT(event.curr.abort_cb);
        event.curr.abort_cb = NULL;

        param = event.curr.param;
        event.curr.param = NULL;

#if defined(CONFIG_BT_CTLR_LOW_LAT_ULL_DONE)
        done_inc();
#endif /* CONFIG_BT_CTLR_LOW_LAT_ULL_DONE */

        if (param) {
            ull = HDR_LLL2ULL(param);
        }

        if (IS_ENABLED(CONFIG_BT_CTLR_LOW_LAT) &&
            (CONFIG_BT_CTLR_LLL_PRIO == CONFIG_BT_CTLR_ULL_LOW_PRIO)) {
            mayfly_enable(TICKER_USER_ID_LLL,
                          TICKER_USER_ID_ULL_LOW,
                          1);
        }

        DEBUG_RADIO_CLOSE(0);
    } else {
        ull = HDR_LLL2ULL(param);
    }

#if !defined(CONFIG_BT_CTLR_LOW_LAT_ULL_DONE)
    ull_prepare_dequeue(TICKER_USER_ID_LLL);
#endif /* !CONFIG_BT_CTLR_LOW_LAT_ULL_DONE */

    /* Let ULL know about LLL event done */
    evdone = ull_event_done(ull);
    LL_ASSERT(evdone);

    return 0;
}

#if defined(CONFIG_BT_CTLR_LOW_LAT_ULL_DONE)
void lll_done_ull_inc(void)
{
    LL_ASSERT(event.done.ull_count != event.done.lll_count);
    event.done.ull_count++;
}
#endif /* CONFIG_BT_CTLR_LOW_LAT_ULL_DONE */

bool lll_is_done(void *param, bool *is_resume)
{
    /* FIXME: use param to check */
    (void)param;
    if (is_resume) {
        *is_resume = false;
    }
    return !event.curr.abort_cb;
}

/* ============================================================
 * LLL Clock functions (stubs for NuttX - no HF clock gating)
 * ============================================================ */

int lll_clk_on(void)
{
    return 0;
}

int lll_clk_on_wait(void)
{
    return 0;
}

int lll_clk_off(void)
{
    return 0;
}

int lll_hfclock_on(void)
{
    return 0;
}

int lll_hfclock_on_wait(void)
{
    return 0;
}

int lll_hfclock_off(void)
{
    return 0;
}

int lll_clock_init(void)
{
    return 0;
}

int lll_clock_deinit(void)
{
    return 0;
}

int lll_clock_wait(void)
{
    return 0;
}

uint8_t lll_clock_sca_local_get(void)
{
    return 0;
}

uint32_t lll_clock_ppm_local_get(void)
{
    return 0;
}

uint32_t lll_clock_ppm_get(uint8_t sca)
{
    (void)sca;
    return 0;
}

/* ============================================================
 * LLL Event Offset / Preempt
 * Reference: openisa lll.c
 * ============================================================ */

uint32_t lll_event_offset_get(struct ull_hdr *ull)
{
    if (0) {
#if defined(CONFIG_BT_CTLR_XTAL_ADVANCED)
    } else if (ull->ticks_prepare_to_start & XON_BITMASK) {
        return MAX(ull->ticks_active_to_start,
                   ull->ticks_preempt_to_start);
#endif /* CONFIG_BT_CTLR_XTAL_ADVANCED */
    } else {
        return MAX(ull->ticks_active_to_start,
                   ull->ticks_prepare_to_start);
    }
}

uint32_t lll_preempt_calc(struct ull_hdr *ull, uint8_t ticker_id,
                          uint32_t ticks_at_event)
{
    uint32_t ticks_now;
    uint32_t diff;

    ticks_now = ticker_ticks_now_get();
    diff = ticks_now - ticks_at_event;
    if (diff & BIT(HAL_TICKER_CNTR_MSBIT)) {
        return 0;
    }

    diff += HAL_TICKER_CNTR_CMP_OFFSET_MIN;
    if (diff > HAL_TICKER_US_TO_TICKS(EVENT_OVERHEAD_START_US)) {
        return 1;
    }

    return 0;
}

/* ============================================================
 * LLL Channel Set
 * Reference: openisa lll.c lll_chan_set()
 * ============================================================ */

void lll_chan_set(uint32_t chan)
{
    switch (chan) {
    case 37:
        radio_freq_chan_set(2);
        break;

    case 38:
        radio_freq_chan_set(26);
        break;

    case 39:
        radio_freq_chan_set(80);
        break;

    default:
        if (chan < 11) {
            radio_freq_chan_set(4 + (chan * 2U));
        } else if (chan < 40) {
            radio_freq_chan_set(28 + ((chan - 11) * 2U));
        } else {
            LL_ASSERT(0);
        }
        break;
    }

    radio_whiten_iv_set(chan);
}

/* ============================================================
 * Radio State Functions
 * ============================================================ */

uint32_t lll_radio_is_idle(void)
{
    return radio_is_idle();
}

uint32_t lll_radio_tx_ready_delay_get(uint8_t phy, uint8_t flags)
{
    return radio_tx_ready_delay_get(phy, flags);
}

uint32_t lll_radio_rx_ready_delay_get(uint8_t phy, uint8_t flags)
{
    return radio_rx_ready_delay_get(phy, flags);
}

int8_t lll_radio_tx_pwr_min_get(void)
{
    return -40;
}

int8_t lll_radio_tx_pwr_max_get(void)
{
    return 8;
}

int8_t lll_radio_tx_pwr_floor(int8_t tx_pwr_lvl)
{
    return tx_pwr_lvl;
}

/* ============================================================
 * ISR Status Functions
 * Reference: openisa lll.c lll_isr_status_reset()
 * ============================================================ */

void lll_isr_status_reset(void)
{
    radio_status_reset();
    radio_tmr_status_reset();
    radio_filter_status_reset();
    if (IS_ENABLED(CONFIG_BT_CTLR_PRIVACY)) {
        radio_ar_status_reset();
    }
    radio_rssi_status_reset();
}

void lll_isr_tx_status_reset(void)
{
    lll_isr_status_reset();
}

void lll_isr_rx_status_reset(void)
{
    lll_isr_status_reset();
}

void lll_isr_tx_sub_status_reset(void)
{
    lll_isr_status_reset();
}

void lll_isr_rx_sub_status_reset(void)
{
    lll_isr_status_reset();
}

void lll_isr_abort(void *param)
{
    radio_disable();
    lll_isr_status_reset();
    lll_done(NULL);
    (void)param;
}

void lll_isr_done(void *param)
{
    lll_isr_status_reset();
    lll_done(NULL);
    (void)param;
}

void lll_isr_cleanup(void *param)
{
    int err;

    radio_isr_set(NULL, NULL);

    err = lll_hfclock_off();
    LL_ASSERT(err >= 0);

    lll_done(NULL);
    (void)param;
}

void lll_isr_early_abort(void *param)
{
    radio_disable();
    lll_done(NULL);
    (void)param;
}

/* ============================================================
 * LLL Prepare Resolve
 * Reference: openisa lll.c lll_prepare_resolve()
 * ============================================================ */

int lll_prepare_resolve(lll_is_abort_cb_t is_abort_cb, lll_abort_cb_t abort_cb,
                        lll_prepare_cb_t prepare_cb,
                        struct lll_prepare_param *prepare_param,
                        uint8_t is_resume, uint8_t is_dequeue)
{
    struct lll_event *p;
    uint8_t idx;
    int err;

    /* Find the ready prepare in the pipeline */
    idx = UINT8_MAX;
    p = ull_prepare_dequeue_iter(&idx);
    while (p && (p->is_aborted || p->is_resume)) {
        p = ull_prepare_dequeue_iter(&idx);
    }

    /* Current event active or another prepare is ready in the pipeline */
    if ((!is_dequeue && !is_done_sync()) ||
        event.curr.abort_cb ||
        (p && is_resume)) {
#if defined(CONFIG_BT_CTLR_LOW_LAT)
        lll_prepare_cb_t resume_cb;
#endif /* CONFIG_BT_CTLR_LOW_LAT */
        struct lll_event *next;

        if (IS_ENABLED(CONFIG_BT_CTLR_LOW_LAT) && event.curr.param) {
            /* early abort */
            event.curr.abort_cb(NULL, event.curr.param);
        }

        /* Store the next prepare for deferred call */
        next = ull_prepare_enqueue(is_abort_cb, abort_cb, prepare_param,
                                   prepare_cb, is_resume);
        LL_ASSERT(next);

#if !defined(CONFIG_BT_CTLR_LOW_LAT)
        if (is_resume) {
            return -EINPROGRESS;
        }

        /* Start the preempt timeout */
        {
            uint32_t ret;

            ret = preempt_ticker_start(next, ticker_start_op_cb);
            LL_ASSERT((ret == TICKER_STATUS_SUCCESS) ||
                      (ret == TICKER_STATUS_BUSY));
        }

#else /* CONFIG_BT_CTLR_LOW_LAT */
        next = NULL;
        while (p) {
            if (!p->is_aborted) {
                if (event.curr.param ==
                    p->prepare_param.param) {
                    p->is_aborted = 1;
                    p->abort_cb(&p->prepare_param,
                                p->prepare_param.param);
                } else {
                    next = p;
                }
            }

            p = ull_prepare_dequeue_iter(&idx);
        }

        if (next) {
            /* check if resume requested by curr */
            err = event.curr.is_abort_cb(NULL, event.curr.param,
                                         &resume_cb);
            LL_ASSERT(err);

            if (err == -EAGAIN) {
                next = resume_enqueue(resume_cb);
                LL_ASSERT(next);
            } else {
                LL_ASSERT(err == -ECANCELED);
            }
        }
#endif /* CONFIG_BT_CTLR_LOW_LAT */

        return -EINPROGRESS;
    }

    LL_ASSERT(!p || &p->prepare_param == prepare_param);

    event.curr.param = prepare_param->param;
    event.curr.is_abort_cb = is_abort_cb;
    event.curr.abort_cb = abort_cb;

    err = prepare_cb(prepare_param);

#if !defined(CONFIG_BT_CTLR_LOW_LAT)
    {
        uint32_t ret;

        /* Stop any scheduled preempt ticker */
        ret = ticker_stop(TICKER_INSTANCE_ID_CTLR,
                          TICKER_USER_ID_LLL,
                          TICKER_ID_LLL_PREEMPT,
                          ticker_stop_op_cb, NULL);
        LL_ASSERT((ret == TICKER_STATUS_SUCCESS) ||
                  (ret == TICKER_STATUS_FAILURE) ||
                  (ret == TICKER_STATUS_BUSY));

        /* Find next prepare needing preempt timeout to be setup */
        do {
            p = ull_prepare_dequeue_iter(&idx);
            if (!p) {
                return err;
            }
        } while (p->is_aborted || p->is_resume);

        /* Start the preempt timeout */
        ret = preempt_ticker_start(p, ticker_start_next_op_cb);
        LL_ASSERT((ret == TICKER_STATUS_SUCCESS) ||
                  (ret == TICKER_STATUS_BUSY));
    }
#endif /* !CONFIG_BT_CTLR_LOW_LAT */

    return err;
}

int lll_is_abort_cb(void *next, void *curr, lll_prepare_cb_t *resume_cb)
{
    (void)next;
    (void)curr;
    (void)resume_cb;
    return -ECANCELED;
}

void lll_abort_cb(struct lll_prepare_param *prepare_param, void *param)
{
    int err;

    /* NOTE: This is not a prepare being cancelled */
    if (!prepare_param) {
        /* Perform event abort here.
         * After event has been cleanly aborted, clean up resources
         * and dispatch event done.
         */
        radio_isr_set(lll_isr_done, NULL);
        radio_disable();
        return;
    }

    /* NOTE: Else clean the top half preparations of the aborted event
     * currently in preparation pipeline.
     */
    err = lll_hfclock_off();
    LL_ASSERT(err >= 0);

    lll_done(param);
}

/* ============================================================
 * LLL Advertising / Scan / Conn reset stubs
 * ============================================================ */

int lll_adv_reset(void)
{
    return 0;
}

int lll_scan_reset(void)
{
    return 0;
}

int lll_conn_reset(void)
{
    return 0;
}

/* ============================================================
 * LLL Advertising PDU functions
 * ============================================================ */

int lll_adv_data_init(struct lll_adv_pdu *pdu)
{
    if (!pdu) {
        return -EINVAL;
    }
    pdu->first = 0;
    pdu->last = 0;
    pdu->pdu[0] = NULL;
    pdu->pdu[1] = NULL;
    return 0;
}

int lll_adv_data_reset(struct lll_adv_pdu *pdu)
{
    return lll_adv_data_init(pdu);
}

int lll_adv_data_dequeue(struct lll_adv_pdu *pdu)
{
    if (!pdu) {
        return -EINVAL;
    }
    pdu->first = pdu->last;
    return 0;
}

int lll_adv_data_release(struct lll_adv_pdu *pdu)
{
    if (!pdu) {
        return -EINVAL;
    }
    pdu->first = 0;
    pdu->last = 0;
    return 0;
}

struct pdu_adv *lll_adv_pdu_alloc(struct lll_adv_pdu *pdu, uint8_t *idx)
{
    uint8_t last;

    if (!pdu || !idx) {
        return NULL;
    }

    last = pdu->last + 1;
    if (last >= DOUBLE_BUFFER_SIZE) {
        last = 0;
    }

    if (pdu->pdu[last]) {
        *idx = last;
        return (struct pdu_adv *)pdu->pdu[last];
    }

    *idx = last;
    return NULL;
}

struct pdu_adv *lll_adv_pdu_alloc_pdu_adv(void)
{
    return NULL;
}

#if defined(CONFIG_BT_CTLR_ADV_PERIODIC)
int lll_adv_and_extra_data_release(struct lll_adv_pdu *pdu)
{
    return lll_adv_data_release(pdu);
}

struct pdu_adv *lll_adv_pdu_and_extra_data_alloc(struct lll_adv_pdu *pdu,
                                                  void **extra_data,
                                                  uint8_t *idx)
{
    if (extra_data) {
        *extra_data = NULL;
    }
    return lll_adv_pdu_alloc(pdu, idx);
}
#endif /* CONFIG_BT_CTLR_ADV_PERIODIC */

#if defined(CONFIG_BT_CTLR_ADV_PDU_LINK)
void lll_adv_pdu_linked_release_all(struct pdu_adv *pdu_first)
{
    (void)pdu_first;
}
#endif /* CONFIG_BT_CTLR_ADV_PDU_LINK */

/* ============================================================
 * Internal helper functions
 * ============================================================ */

static int init_reset(void)
{
    return 0;
}

#if defined(CONFIG_BT_CTLR_LOW_LAT_ULL_DONE)
static inline void done_inc(void)
{
    event.done.lll_count++;
    LL_ASSERT(event.done.lll_count != event.done.ull_count);
}
#endif /* CONFIG_BT_CTLR_LOW_LAT_ULL_DONE */

static inline bool is_done_sync(void)
{
#if defined(CONFIG_BT_CTLR_LOW_LAT_ULL_DONE)
    return event.done.lll_count == event.done.ull_count;
#else
    return true;
#endif
}

static struct lll_event *resume_enqueue(lll_prepare_cb_t resume_cb)
{
    (void)resume_cb;
    return NULL;
}

/* ============================================================
 * Preempt ticker functions
 * ============================================================ */

#if !defined(CONFIG_BT_CTLR_LOW_LAT)
static void ticker_stop_op_cb(uint32_t status, void *param)
{
    (void)status;
    (void)param;
}

static void ticker_start_op_cb(uint32_t status, void *param)
{
    (void)status;
    (void)param;
}

static void ticker_start_next_op_cb(uint32_t status, void *param)
{
    (void)status;
    (void)param;
}

static uint32_t preempt_ticker_start(struct lll_event *evt,
                                     ticker_op_func op_cb)
{
    uint32_t ret;
    uint32_t ticks_at_event;
    uint32_t preempt_anchor;
    struct ull_hdr *ull;

    ull = HDR_LLL2ULL(evt->prepare_param.param);
    ticks_at_event = evt->prepare_param.ticks_at_expire;
    preempt_anchor = ticks_at_event;

    ret = ticker_start(TICKER_INSTANCE_ID_CTLR,
                       TICKER_USER_ID_LLL,
                       TICKER_ID_LLL_PREEMPT,
                       preempt_anchor,
                       HAL_TICKER_US_TO_TICKS(EVENT_OVERHEAD_PREEMPT_US),
                       TICKER_NULL_PERIOD,
                       TICKER_NULL_REMAINDER,
                       TICKER_NULL_LAZY,
                       TICKER_NULL_SLOT,
                       preempt_ticker_cb, NULL,
                       op_cb, NULL);

    return ret;
}

static void preempt_ticker_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
                              uint32_t remainder, uint16_t lazy, uint8_t force,
                              void *param)
{
    static memq_link_t link;
    static struct mayfly mfy = {0, 0, &link, NULL, preempt};
    uint32_t ret;

    (void)ticks_at_expire;
    (void)ticks_drift;
    (void)remainder;
    (void)lazy;
    (void)force;
    (void)param;

    ret = mayfly_enqueue(TICKER_USER_ID_ULL_HIGH, TICKER_USER_ID_LLL,
                         0, &mfy);
    LL_ASSERT(!ret);
}

static void preempt(void *param)
{
    struct lll_event *next;
    uint8_t idx;

    (void)param;

    if (!event.curr.abort_cb || !event.curr.param) {
        return;
    }

    idx = UINT8_MAX;
    next = ull_prepare_dequeue_iter(&idx);
    if (!next) {
        return;
    }

    while (next && (next->is_aborted || next->is_resume)) {
        next = ull_prepare_dequeue_iter(&idx);
    }

    if (!next) {
        return;
    }

    event.curr.abort_cb(NULL, event.curr.param);
}
#else /* CONFIG_BT_CTLR_LOW_LAT */
#if (CONFIG_BT_CTLR_LLL_PRIO == CONFIG_BT_CTLR_ULL_LOW_PRIO)
static void mfy_ticker_job_idle_get(void *param)
{
    uint32_t ret;

    (void)param;

    ret = ticker_job_idle_get(TICKER_INSTANCE_ID_CTLR,
                              TICKER_USER_ID_ULL_LOW,
                              ticker_op_job_disable, NULL);
    LL_ASSERT((ret == TICKER_STATUS_SUCCESS) ||
              (ret == TICKER_STATUS_BUSY));
}

static void ticker_op_job_disable(uint32_t status, void *op_context)
{
    (void)status;
    (void)op_context;

    mayfly_enable(TICKER_USER_ID_ULL_LOW, TICKER_USER_ID_ULL_LOW, 0);
}
#endif
#endif /* CONFIG_BT_CTLR_LOW_LAT */
