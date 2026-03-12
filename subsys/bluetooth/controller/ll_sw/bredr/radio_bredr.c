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
 * BR/EDR Radio Hardware Abstraction Layer Implementation
 * Bluetooth Core Spec Vol 2, Part B: Baseband Specification
 */

#include <zephyr/kernel.h>
#include <string.h>

#include "radio_bredr.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_ctlr_radio_bredr, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

/*
 * Static variables
 */
static radio_bredr_isr_cb_t radio_isr_cb;
static void *radio_isr_param;
static enum radio_bredr_state radio_state;
static struct radio_bredr_config radio_config;
static struct radio_bredr_stats radio_stats;

/* TX/RX buffers */
static uint8_t rx_buffer[RADIO_BREDR_DH5_MAX_LEN + 10];
static uint8_t tx_buffer[RADIO_BREDR_DH5_MAX_LEN + 10];
static uint8_t empty_pkt[10];
static uint8_t scratch_buffer[RADIO_BREDR_DH5_MAX_LEN + 10];

/* Current packet pointers */
static void *rx_pkt_ptr;
static void *tx_pkt_ptr;

/*
 * BCH/Parity tables for access code generation
 * Bluetooth Core Spec Vol 2, Part B, Section 6.3
 */
static const uint8_t pn_sequence[127] = {
	1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0,
	0, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1,
	1, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1, 1,
	1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0,
	0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1,
	0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0,
	1, 0, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1,
	1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1
};

/*
 * CRC-16 polynomial for BR/EDR: x^16 + x^12 + x^5 + 1
 */
#define CRC16_POLY 0x8005

/*
 * HEC polynomial: x^8 + x^7 + x^5 + x^2 + x + 1
 */
#define HEC_POLY 0xA7

/*
 * Initialization
 */
int radio_bredr_init(void)
{
	memset(&radio_config, 0, sizeof(radio_config));
	memset(&radio_stats, 0, sizeof(radio_stats));

	radio_state = RADIO_BREDR_STATE_DISABLED;
	radio_isr_cb = NULL;
	radio_isr_param = NULL;

	rx_pkt_ptr = rx_buffer;
	tx_pkt_ptr = tx_buffer;

	/* Initialize empty packet (NULL packet) */
	memset(empty_pkt, 0, sizeof(empty_pkt));

	LOG_DBG("BR/EDR radio initialized");
	return 0;
}

int radio_bredr_reset(void)
{
	radio_bredr_disable();
	memset(&radio_stats, 0, sizeof(radio_stats));
	radio_state = RADIO_BREDR_STATE_DISABLED;

	LOG_DBG("BR/EDR radio reset");
	return 0;
}

/*
 * ISR Management
 */
void radio_bredr_isr_set(radio_bredr_isr_cb_t cb, void *param)
{
	radio_isr_cb = cb;
	radio_isr_param = param;
}

/* Internal ISR handler - called by hardware interrupt */
void radio_bredr_isr(void)
{
	if (radio_isr_cb) {
		radio_isr_cb(radio_isr_param);
	}
}

/*
 * Configuration
 */
void radio_bredr_freq_set(uint8_t channel)
{
	if (channel >= RADIO_BREDR_CHANNEL_COUNT) {
		LOG_WRN("Invalid channel: %u", channel);
		return;
	}

	radio_config.channel = channel;

	/* Actual frequency = 2402 + channel MHz */
	uint32_t freq_mhz = RADIO_BREDR_FREQ_BASE + channel;

	LOG_DBG("Set frequency: channel=%u, freq=%u MHz", channel, freq_mhz);

	/* Platform-specific radio configuration
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 *
	 * For Nordic nRF52/nRF53:
	 *   NRF_RADIO->FREQUENCY = (freq_mhz - 2400);
	 *
	 * For other platforms, implement the appropriate register writes.
	 */
}

void radio_bredr_access_code_set(const uint8_t *access_code)
{
	memcpy(radio_config.access_code, access_code, 9);

	LOG_DBG("Set access code: %02x%02x%02x%02x%02x%02x%02x%02x%02x",
		access_code[0], access_code[1], access_code[2],
		access_code[3], access_code[4], access_code[5],
		access_code[6], access_code[7], access_code[8]);

	/* Platform-specific access code configuration
	 * Configure radio hardware for access code correlation.
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 */
}

void radio_bredr_tx_power_set(int8_t power)
{
	radio_config.tx_power = power;

	LOG_DBG("Set TX power: %d dBm", power);

	/* Platform-specific TX power configuration
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 */
}

void radio_bredr_whitening_set(uint8_t enable)
{
	radio_config.whitening = enable;

	LOG_DBG("Whitening: %s", enable ? "enabled" : "disabled");
}

/*
 * Packet Configuration
 */
void radio_bredr_pkt_configure(uint8_t packet_type, uint8_t max_len)
{
	radio_config.packet_type = packet_type;

	LOG_DBG("Packet config: type=0x%02x, max_len=%u", packet_type, max_len);

	/* Platform-specific packet format configuration
	 * Configure radio packet format based on packet type.
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 */
}

void radio_bredr_pkt_rx_set(void *rx_packet)
{
	rx_pkt_ptr = rx_packet ? rx_packet : rx_buffer;

	/* Platform-specific DMA configuration for RX
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 */
}

void radio_bredr_pkt_tx_set(void *tx_packet)
{
	tx_pkt_ptr = tx_packet ? tx_packet : tx_buffer;

	/* Platform-specific DMA configuration for TX
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 */
}

/*
 * Radio Control
 */
void radio_bredr_rx_enable(void)
{
	radio_state = RADIO_BREDR_STATE_RX_IDLE;

	LOG_DBG("RX enabled");

	/* Platform-specific radio RX enable
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 *
	 * For Nordic nRF52/nRF53:
	 *   NRF_RADIO->TASKS_RXEN = 1;
	 */
}

void radio_bredr_tx_enable(void)
{
	radio_state = RADIO_BREDR_STATE_TX_IDLE;

	LOG_DBG("TX enabled");

	/* Platform-specific radio TX enable
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 *
	 * For Nordic nRF52/nRF53:
	 *   NRF_RADIO->TASKS_TXEN = 1;
	 */
}

void radio_bredr_disable(void)
{
	radio_state = RADIO_BREDR_STATE_DISABLED;

	LOG_DBG("Radio disabled");

	/* Platform-specific radio disable
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 *
	 * For Nordic nRF52/nRF53:
	 *   NRF_RADIO->TASKS_DISABLE = 1;
	 */
}

/*
 * Status
 */
uint32_t radio_bredr_is_ready(void)
{
	/* Platform-specific radio ready status check
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 */
	return (radio_state == RADIO_BREDR_STATE_RX_IDLE ||
		radio_state == RADIO_BREDR_STATE_TX_IDLE);
}

uint32_t radio_bredr_is_done(void)
{
	/* Platform-specific radio done status check
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 * Returns 1 when TX/RX operation is complete.
	 */
	return 0;
}

uint32_t radio_bredr_crc_is_valid(void)
{
	/* Platform-specific CRC status check from hardware
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 * Returns 1 if CRC is valid.
	 */
	return 1;
}

uint32_t radio_bredr_hec_is_valid(void)
{
	/* Platform-specific HEC status check
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 * Returns 1 if HEC is valid.
	 */
	return 1;
}

int8_t radio_bredr_rssi_get(void)
{
	/* Platform-specific RSSI read from hardware
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 */
	return radio_stats.last_rssi;
}

/*
 * Timing
 */
void radio_bredr_tmr_start(uint32_t ticks_start)
{
	/* Platform-specific radio timer start
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 */
	LOG_DBG("Timer start: ticks=%u", ticks_start);
}

void radio_bredr_tmr_stop(void)
{
	/* Platform-specific radio timer stop
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 */
	LOG_DBG("Timer stop");
}

uint32_t radio_bredr_tmr_end_get(void)
{
	/* Platform-specific timer end value read
	 * This is a placeholder - actual implementation depends on the radio peripheral.
	 */
	return 0;
}

/*
 * Buffer Access
 */
void *radio_bredr_pkt_empty_get(void)
{
	return empty_pkt;
}

void *radio_bredr_pkt_scratch_get(void)
{
	return scratch_buffer;
}

/*
 * Access Code Generation
 * Bluetooth Core Spec Vol 2, Part B, Section 6.3
 */

/**
 * @brief Generate sync word from LAP using BCH encoding
 *
 * The sync word is 64 bits derived from the 24-bit LAP using
 * a (64,30) shortened BCH code.
 */
static void sync_word_generate(uint32_t lap, uint8_t *sync_word)
{
	uint64_t sw = 0;
	uint8_t barker[7] = {1, 1, 1, 0, 0, 1, 0};  /* Barker sequence */
	int i;

	/* The sync word generation is complex - simplified version here */
	/* Full implementation requires BCH(64,30) encoding */

	/* For now, use a simplified approach */
	sw = lap;
	sw |= ((uint64_t)(lap ^ 0xFFFFFF) << 24);
	sw |= ((uint64_t)0x55 << 56);  /* Preamble pattern */

	/* Pack into sync_word buffer */
	for (i = 0; i < 8; i++) {
		sync_word[i] = (sw >> (i * 8)) & 0xFF;
	}
}

void radio_bredr_dac_generate(const uint8_t *bd_addr, uint8_t *access_code)
{
	uint32_t lap;

	/* Extract LAP from BD_ADDR (lower 24 bits) */
	lap = bd_addr[0] | ((uint32_t)bd_addr[1] << 8) |
	      ((uint32_t)bd_addr[2] << 16);

	/* Generate access code */
	/* Preamble (4 bits) + Sync Word (64 bits) + Trailer (4 bits) = 72 bits */

	/* Preamble depends on MSB of sync word */
	access_code[0] = 0x55;  /* Alternating pattern */

	/* Generate sync word from LAP */
	sync_word_generate(lap, &access_code[1]);

	LOG_DBG("DAC generated for LAP=0x%06x", lap);
}

void radio_bredr_cac_generate(const uint8_t *bd_addr, uint8_t *access_code)
{
	/* CAC is same as DAC for the Central's BD_ADDR */
	radio_bredr_dac_generate(bd_addr, access_code);
}

void radio_bredr_iac_generate(uint32_t lap, uint8_t *access_code)
{
	/* IAC uses reserved LAPs:
	 * GIAC: 0x9E8B33
	 * LIAC: 0x9E8B00 - 0x9E8B3F
	 */

	/* Preamble */
	access_code[0] = 0x55;

	/* Generate sync word from LAP */
	sync_word_generate(lap, &access_code[1]);

	LOG_DBG("IAC generated for LAP=0x%06x", lap);
}

/*
 * HEC Calculation
 * Bluetooth Core Spec Vol 2, Part B, Section 7.1.1
 *
 * HEC is an 8-bit CRC over the 10-bit header using polynomial
 * g(x) = x^8 + x^7 + x^5 + x^2 + x + 1
 */
uint8_t radio_bredr_hec_calc(uint8_t *header, uint8_t len)
{
	uint8_t hec = 0;
	int i, j;

	for (i = 0; i < len; i++) {
		uint8_t byte = header[i];
		for (j = 0; j < 8; j++) {
			uint8_t bit = (byte >> j) & 1;
			uint8_t feedback = (hec >> 7) ^ bit;
			hec = (hec << 1) ^ (feedback ? HEC_POLY : 0);
		}
	}

	return hec;
}

int radio_bredr_hec_verify(uint8_t *header, uint8_t len, uint8_t hec)
{
	uint8_t calc_hec = radio_bredr_hec_calc(header, len);
	return (calc_hec == hec) ? 0 : -1;
}

/*
 * CRC-16 Calculation
 * Bluetooth Core Spec Vol 2, Part B, Section 7.2
 *
 * CRC-16 using polynomial g(x) = x^16 + x^12 + x^5 + 1
 * Initial value depends on UAP
 */
uint16_t radio_bredr_crc_calc(uint8_t *data, uint16_t len)
{
	uint16_t crc = 0xFFFF;  /* Initial value */
	int i, j;

	for (i = 0; i < len; i++) {
		crc ^= ((uint16_t)data[i] << 8);
		for (j = 0; j < 8; j++) {
			if (crc & 0x8000) {
				crc = (crc << 1) ^ CRC16_POLY;
			} else {
				crc <<= 1;
			}
		}
	}

	return crc;
}

int radio_bredr_crc_verify(uint8_t *data, uint16_t len, uint16_t crc)
{
	uint16_t calc_crc = radio_bredr_crc_calc(data, len);
	return (calc_crc == crc) ? 0 : -1;
}

/*
 * Data Whitening
 * Bluetooth Core Spec Vol 2, Part B, Section 7.3
 *
 * Uses LFSR with polynomial x^7 + x^4 + 1
 * Initialized with clock bits CLK6-0
 */
void radio_bredr_whiten(uint8_t *data, uint16_t len, uint8_t clock)
{
	uint8_t lfsr = clock & 0x7F;  /* 7-bit LFSR */
	int i, j;

	for (i = 0; i < len; i++) {
		uint8_t whitened = 0;
		for (j = 0; j < 8; j++) {
			/* XOR data bit with LFSR output */
			uint8_t bit = (data[i] >> j) & 1;
			uint8_t lfsr_out = lfsr & 1;
			whitened |= ((bit ^ lfsr_out) << j);

			/* Advance LFSR */
			uint8_t feedback = ((lfsr >> 6) ^ (lfsr >> 3)) & 1;
			lfsr = (lfsr >> 1) | (feedback << 6);
		}
		data[i] = whitened;
	}
}

void radio_bredr_dewhiten(uint8_t *data, uint16_t len, uint8_t clock)
{
	/* Whitening is self-inverse */
	radio_bredr_whiten(data, len, clock);
}

/*
 * FEC Encoding/Decoding
 * Bluetooth Core Spec Vol 2, Part B, Section 7.4
 *
 * 1/3 rate FEC for DM packets using (15,10) shortened Hamming code
 */
void radio_bredr_fec_encode(uint8_t *data, uint16_t len, uint8_t *encoded)
{
	/* Simplified FEC encoding - each byte becomes 3 bytes */
	int i;

	for (i = 0; i < len; i++) {
		/* Simple repetition code as placeholder */
		/* Real implementation uses (15,10) Hamming code */
		encoded[i * 3] = data[i];
		encoded[i * 3 + 1] = data[i];
		encoded[i * 3 + 2] = data[i];
	}
}

int radio_bredr_fec_decode(uint8_t *encoded, uint16_t len, uint8_t *data)
{
	/* Simplified FEC decoding with majority voting */
	int i;
	int errors = 0;

	for (i = 0; i < len / 3; i++) {
		uint8_t b0 = encoded[i * 3];
		uint8_t b1 = encoded[i * 3 + 1];
		uint8_t b2 = encoded[i * 3 + 2];

		/* Majority voting for each bit */
		uint8_t result = 0;
		int j;
		for (j = 0; j < 8; j++) {
			uint8_t bit0 = (b0 >> j) & 1;
			uint8_t bit1 = (b1 >> j) & 1;
			uint8_t bit2 = (b2 >> j) & 1;
			uint8_t majority = (bit0 + bit1 + bit2) >= 2 ? 1 : 0;
			result |= (majority << j);

			/* Count errors */
			if (bit0 != majority || bit1 != majority || bit2 != majority) {
				errors++;
			}
		}
		data[i] = result;
	}

	return errors;
}
