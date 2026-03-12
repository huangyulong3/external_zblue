/****************************************************************************
 *  Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ***************************************************************************/

/*
 * BR/EDR Radio Hardware Abstraction Layer
 * Bluetooth Core Spec Vol 2, Part B: Baseband Specification
 *
 * This provides a radio abstraction for BR/EDR operations.
 * BR/EDR uses GFSK modulation at 1 Mbps (Basic Rate) or
 * π/4-DQPSK/8DPSK at 2/3 Mbps (Enhanced Data Rate).
 */

#ifndef SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_RADIO_BREDR_H_
#define SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_RADIO_BREDR_H_

#include <zephyr/types.h>

/*
 * BR/EDR Radio Constants
 */

/* BR/EDR frequency range: 2402-2480 MHz (79 channels, 1 MHz spacing) */
#define RADIO_BREDR_FREQ_BASE       2402
#define RADIO_BREDR_FREQ_MAX        2480
#define RADIO_BREDR_CHANNEL_COUNT   79

/* Packet types */
#define RADIO_BREDR_PKT_NULL        0x00
#define RADIO_BREDR_PKT_POLL        0x01
#define RADIO_BREDR_PKT_FHS         0x02
#define RADIO_BREDR_PKT_DM1         0x03
#define RADIO_BREDR_PKT_DH1         0x04
#define RADIO_BREDR_PKT_HV1         0x05
#define RADIO_BREDR_PKT_HV2         0x06
#define RADIO_BREDR_PKT_HV3         0x07
#define RADIO_BREDR_PKT_DV          0x08
#define RADIO_BREDR_PKT_AUX1        0x09
#define RADIO_BREDR_PKT_DM3         0x0A
#define RADIO_BREDR_PKT_DH3         0x0B
#define RADIO_BREDR_PKT_EV4         0x0C
#define RADIO_BREDR_PKT_EV5         0x0D
#define RADIO_BREDR_PKT_DM5         0x0E
#define RADIO_BREDR_PKT_DH5         0x0F

/* EDR packet types (2/3 Mbps) */
#define RADIO_BREDR_PKT_2DH1        0x04
#define RADIO_BREDR_PKT_2EV3        0x06
#define RADIO_BREDR_PKT_2DH3        0x0A
#define RADIO_BREDR_PKT_2EV5        0x0C
#define RADIO_BREDR_PKT_2DH5        0x0E
#define RADIO_BREDR_PKT_3EV3        0x07
#define RADIO_BREDR_PKT_3DH1        0x08
#define RADIO_BREDR_PKT_3DH3        0x0B
#define RADIO_BREDR_PKT_3EV5        0x0D
#define RADIO_BREDR_PKT_3DH5        0x0F

/* Slot durations in microseconds */
#define RADIO_BREDR_SLOT_US         625
#define RADIO_BREDR_HALF_SLOT_US    312

/* Access code length */
#define RADIO_BREDR_ACCESS_CODE_LEN 72  /* bits */

/* Packet header length */
#define RADIO_BREDR_PKT_HDR_LEN     54  /* bits */

/* CRC length */
#define RADIO_BREDR_CRC_LEN         16  /* bits */

/* Max payload sizes */
#define RADIO_BREDR_DM1_MAX_LEN     17
#define RADIO_BREDR_DH1_MAX_LEN     27
#define RADIO_BREDR_DM3_MAX_LEN     121
#define RADIO_BREDR_DH3_MAX_LEN     183
#define RADIO_BREDR_DM5_MAX_LEN     224
#define RADIO_BREDR_DH5_MAX_LEN     339

/* LMP PDU max size */
#define RADIO_BREDR_LMP_MAX_LEN     17

/*
 * Radio ISR callback type
 */
typedef void (*radio_bredr_isr_cb_t)(void *param);

/*
 * Radio State
 */
enum radio_bredr_state {
	RADIO_BREDR_STATE_DISABLED = 0,
	RADIO_BREDR_STATE_RX_IDLE,
	RADIO_BREDR_STATE_RX_ACTIVE,
	RADIO_BREDR_STATE_TX_IDLE,
	RADIO_BREDR_STATE_TX_ACTIVE,
};

/*
 * TX/RX Buffer Structure
 */
struct radio_bredr_pkt {
	uint8_t lt_addr;        /* Logical Transport Address (3 bits) */
	uint8_t type;           /* Packet type (4 bits) */
	uint8_t flow;           /* Flow control bit */
	uint8_t arqn;           /* Acknowledgment bit */
	uint8_t seqn;           /* Sequence number bit */
	uint8_t hec;            /* Header Error Check (8 bits) */
	uint8_t payload_len;    /* Payload length */
	uint8_t payload[339];   /* Max DH5 payload */
	uint16_t crc;           /* CRC-16 */
};

/*
 * Radio Configuration
 */
struct radio_bredr_config {
	uint8_t channel;        /* RF channel (0-78) */
	uint8_t access_code[9]; /* 72-bit access code */
	int8_t tx_power;        /* TX power in dBm */
	uint8_t whitening;      /* Whitening enabled */
	uint8_t packet_type;    /* Expected packet type */
};

/*
 * Radio Statistics
 */
struct radio_bredr_stats {
	uint32_t tx_count;
	uint32_t rx_count;
	uint32_t crc_errors;
	uint32_t hec_errors;
	uint32_t sync_errors;
	int8_t last_rssi;
};

/*
 * Radio HAL Functions
 */

/* Initialization */
int radio_bredr_init(void);
int radio_bredr_reset(void);

/* ISR management */
void radio_bredr_isr_set(radio_bredr_isr_cb_t cb, void *param);

/* Configuration */
void radio_bredr_freq_set(uint8_t channel);
void radio_bredr_access_code_set(const uint8_t *access_code);
void radio_bredr_tx_power_set(int8_t power);
void radio_bredr_whitening_set(uint8_t enable);

/* Packet configuration */
void radio_bredr_pkt_configure(uint8_t packet_type, uint8_t max_len);
void radio_bredr_pkt_rx_set(void *rx_packet);
void radio_bredr_pkt_tx_set(void *tx_packet);

/* Radio control */
void radio_bredr_rx_enable(void);
void radio_bredr_tx_enable(void);
void radio_bredr_disable(void);

/* Status */
uint32_t radio_bredr_is_ready(void);
uint32_t radio_bredr_is_done(void);
uint32_t radio_bredr_crc_is_valid(void);
uint32_t radio_bredr_hec_is_valid(void);
int8_t radio_bredr_rssi_get(void);

/* Timing */
void radio_bredr_tmr_start(uint32_t ticks_start);
void radio_bredr_tmr_stop(void);
uint32_t radio_bredr_tmr_end_get(void);

/* Empty packet buffer */
void *radio_bredr_pkt_empty_get(void);

/* Scratch buffer for temporary storage */
void *radio_bredr_pkt_scratch_get(void);

/*
 * BR/EDR Specific Functions
 */

/* Access code generation */
void radio_bredr_dac_generate(const uint8_t *bd_addr, uint8_t *access_code);
void radio_bredr_cac_generate(const uint8_t *bd_addr, uint8_t *access_code);
void radio_bredr_iac_generate(uint32_t lap, uint8_t *access_code);

/* HEC calculation */
uint8_t radio_bredr_hec_calc(uint8_t *header, uint8_t len);
int radio_bredr_hec_verify(uint8_t *header, uint8_t len, uint8_t hec);

/* CRC calculation */
uint16_t radio_bredr_crc_calc(uint8_t *data, uint16_t len);
int radio_bredr_crc_verify(uint8_t *data, uint16_t len, uint16_t crc);

/* Whitening */
void radio_bredr_whiten(uint8_t *data, uint16_t len, uint8_t clock);
void radio_bredr_dewhiten(uint8_t *data, uint16_t len, uint8_t clock);

/* FEC encoding/decoding (for DM packets) */
void radio_bredr_fec_encode(uint8_t *data, uint16_t len, uint8_t *encoded);
int radio_bredr_fec_decode(uint8_t *encoded, uint16_t len, uint8_t *data);

#endif /* SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_RADIO_BREDR_H_ */
