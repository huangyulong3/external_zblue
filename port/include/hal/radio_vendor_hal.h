/*
 * Copyright (c) 2024 Xiaomi Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * Radio vendor HAL for NuttX port - Socket-based virtual radio
 * This file provides declarations for a software-simulated radio
 * that uses UDP sockets for BLE data transmission in QEMU.
 */

#ifndef HAL_RADIO_VENDOR_HAL_H_
#define HAL_RADIO_VENDOR_HAL_H_

#include <stdint.h>
#include <stdbool.h>

/* Radio power levels */
#define RADIO_TXP_DEFAULT 0

/* Radio modes */
#define RADIO_MODE_1M    0
#define RADIO_MODE_2M    1
#define RADIO_MODE_CODED 2

/* Radio PHY types */
#define RADIO_PHY_1M     0
#define RADIO_PHY_2M     1
#define RADIO_PHY_CODED  2

/* Packet configuration flags */
#define RADIO_PKT_CONF_LENGTH_8BIT  8
#define RADIO_PKT_CONF_PHY(phy)     ((phy) & 0x03)

/* Radio ISR callback type */
typedef void (*radio_isr_cb_t)(void *param);

/* Radio setup and control */
void radio_setup(void);
void radio_reset(void);
void radio_phy_set(uint8_t phy, uint8_t flags);
void radio_tx_power_set(int8_t power);
void radio_freq_chan_set(uint32_t chan);
void radio_whiten_iv_set(uint32_t iv);
void radio_aa_set(const uint8_t *aa);
void radio_pkt_configure(uint8_t bits_len, uint8_t max_len, uint8_t flags);
void radio_pkt_rx_set(void *rx_packet);
void radio_pkt_tx_set(void *tx_packet);
uint32_t radio_tx_ready_delay_get(uint8_t phy, uint8_t flags);
uint32_t radio_rx_ready_delay_get(uint8_t phy, uint8_t flags);
uint32_t radio_is_ready(void);
uint32_t radio_is_done(void);
uint32_t radio_has_disabled(void);
uint32_t radio_is_idle(void);
void radio_crc_configure(uint32_t polynomial, uint32_t iv);
uint32_t radio_crc_is_valid(void);
void radio_rx_enable(void);
void radio_tx_enable(void);
void radio_disable(void);
void radio_status_reset(void);
void radio_tmr_status_reset(void);
void radio_rssi_status_reset(void);
void radio_rssi_measure(void);
uint32_t radio_rssi_is_ready(void);
uint32_t radio_rssi_get(void);
uint32_t radio_is_tx_done(void);
uint32_t radio_phy_flags_rx_get(void);
void *radio_pkt_scratch_get(void);
uint32_t radio_tx_chain_delay_get(uint8_t phy, uint8_t flags);
uint32_t radio_rx_chain_delay_get(uint8_t phy, uint8_t flags);
uint32_t radio_tmr_start_get(void);
uint32_t radio_tmr_tifs_base_get(void);

/* ISR management */
void radio_isr_set(radio_isr_cb_t cb, void *param);

/* Timer functions */
uint32_t radio_tmr_start(uint8_t trx, uint32_t ticks_start,
                         uint32_t remainder);
void radio_tmr_stop(void);
void radio_tmr_hcto_configure(uint32_t hcto);
void radio_tmr_tifs_set(uint32_t tifs);
uint32_t radio_tmr_aa_get(void);
uint32_t radio_tmr_aa_restore(void);
uint32_t radio_tmr_ready_get(void);
uint32_t radio_tmr_end_get(void);
void radio_tmr_end_capture(void);
void radio_tmr_sample(void);
uint32_t radio_tmr_sample_get(void);

/* Radio switch functions */
void radio_switch_complete_and_rx(uint8_t phy);
void radio_switch_complete_and_tx(uint8_t phy, uint8_t flags_rx,
                                  uint8_t phy_tx, uint8_t flags_tx);
void radio_switch_complete_and_disable(void);
void radio_switch_complete_and_b2b_tx(uint8_t phy_curr, uint8_t flags_curr,
                                      uint8_t phy_next, uint8_t flags_next);

/* Filter functions */
void radio_filter_configure(uint8_t bitmask_enable, uint8_t bitmask_addr_type,
                            uint8_t *bdaddr);
void radio_filter_disable(void);
void radio_filter_status_reset(void);
uint32_t radio_filter_has_match(void);
uint32_t radio_filter_match_get(void);

/* GPIO PA/LNA stubs */
#define HAL_RADIO_GPIO_HAVE_PA_PIN  0
#define HAL_RADIO_GPIO_HAVE_LNA_PIN 0

void radio_gpio_pa_setup(void);
void radio_gpio_lna_setup(void);
void radio_gpio_pa_lna_enable(uint32_t trx_us);

/* Address Resolution stubs */
void radio_ar_configure(uint32_t count, uint8_t *irks, uint8_t flags);
uint32_t radio_ar_has_match(void);
uint32_t radio_ar_match_get(void);
void radio_ar_status_reset(void);

/* B2B TX disable */
void radio_switch_complete_and_b2b_tx_disable(void);

/* Socket-based virtual radio API */
int radio_sim_init(void);
void radio_sim_deinit(void);
int radio_sim_send(const void *data, uint16_t len);
int radio_sim_recv(void *data, uint16_t max_len);

#endif /* HAL_RADIO_VENDOR_HAL_H_ */
