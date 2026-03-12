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
 * BR/EDR Lower Link Layer (LLL) Implementation
 * Baseband operations for BR/EDR Controller
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/hci_types.h>

#include "hal/cpu.h"
#include "hal/ticker.h"

#include "util/util.h"
#include "util/mem.h"
#include "util/memq.h"
#include "util/mayfly.h"

#include "ticker/ticker.h"

#include "lll.h"
#include "lll_clock.h"
#include "pdu_bredr.h"
#include "lll_bredr.h"
#include "ull_bredr.h"
#include "lmp_proc.h"
#include "lmp_internal.h"
#include "radio_bredr.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_ctlr_lll_bredr, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

/*
 * BR/EDR PDU Size Constants
 */
#define MAX_BREDR_PDU_SIZE      339U  /* DH5: 339 bytes payload */
#define MAX_LMP_PDU_SIZE        17    /* Max LMP PDU size */

/*
 * BR/EDR Frequency Hopping Kernel
 * Based on Core Spec Vol 2, Part B, Section 2.6
 */

/* Permutation table for frequency hopping */
static const uint8_t perm_table[14][16] = {
	{0x03, 0x04, 0x0A, 0x06, 0x0B, 0x01, 0x0C, 0x07,
	 0x05, 0x08, 0x09, 0x0E, 0x00, 0x0F, 0x0D, 0x02},
	{0x0B, 0x04, 0x0A, 0x06, 0x08, 0x01, 0x0C, 0x07,
	 0x05, 0x00, 0x09, 0x0E, 0x03, 0x0F, 0x0D, 0x02},
	{0x0B, 0x04, 0x0A, 0x06, 0x08, 0x01, 0x0C, 0x07,
	 0x05, 0x00, 0x09, 0x0E, 0x03, 0x0F, 0x0D, 0x02},
	{0x0B, 0x04, 0x0A, 0x06, 0x08, 0x01, 0x0C, 0x07,
	 0x05, 0x00, 0x09, 0x0E, 0x03, 0x0F, 0x0D, 0x02},
	{0x0B, 0x04, 0x0A, 0x06, 0x08, 0x01, 0x0C, 0x07,
	 0x05, 0x00, 0x09, 0x0E, 0x03, 0x0F, 0x0D, 0x02},
	{0x0B, 0x04, 0x0A, 0x06, 0x08, 0x01, 0x0C, 0x07,
	 0x05, 0x00, 0x09, 0x0E, 0x03, 0x0F, 0x0D, 0x02},
	{0x0B, 0x04, 0x0A, 0x06, 0x08, 0x01, 0x0C, 0x07,
	 0x05, 0x00, 0x09, 0x0E, 0x03, 0x0F, 0x0D, 0x02},
	{0x0B, 0x04, 0x0A, 0x06, 0x08, 0x01, 0x0C, 0x07,
	 0x05, 0x00, 0x09, 0x0E, 0x03, 0x0F, 0x0D, 0x02},
	{0x0B, 0x04, 0x0A, 0x06, 0x08, 0x01, 0x0C, 0x07,
	 0x05, 0x00, 0x09, 0x0E, 0x03, 0x0F, 0x0D, 0x02},
	{0x0B, 0x04, 0x0A, 0x06, 0x08, 0x01, 0x0C, 0x07,
	 0x05, 0x00, 0x09, 0x0E, 0x03, 0x0F, 0x0D, 0x02},
	{0x0B, 0x04, 0x0A, 0x06, 0x08, 0x01, 0x0C, 0x07,
	 0x05, 0x00, 0x09, 0x0E, 0x03, 0x0F, 0x0D, 0x02},
	{0x0B, 0x04, 0x0A, 0x06, 0x08, 0x01, 0x0C, 0x07,
	 0x05, 0x00, 0x09, 0x0E, 0x03, 0x0F, 0x0D, 0x02},
	{0x0B, 0x04, 0x0A, 0x06, 0x08, 0x01, 0x0C, 0x07,
	 0x05, 0x00, 0x09, 0x0E, 0x03, 0x0F, 0x0D, 0x02},
	{0x0B, 0x04, 0x0A, 0x06, 0x08, 0x01, 0x0C, 0x07,
	 0x05, 0x00, 0x09, 0x0E, 0x03, 0x0F, 0x0D, 0x02},
};

/*
 * Initialization
 */
int lll_bredr_init(void)
{
	LOG_DBG("BR/EDR LLL initialized");
	return 0;
}

int lll_bredr_reset(void)
{
	LOG_DBG("BR/EDR LLL reset");
	return 0;
}

/*
 * Frequency Hopping Calculation
 * Core Spec Vol 2, Part B, Section 2.6
 */
uint8_t lll_bredr_hop_channel_calc(uint32_t clock, uint8_t *channel_map,
				   uint8_t channel_count, uint32_t uap_lap)
{
	uint32_t x;
	uint8_t y1, y2;
	uint8_t a, b, c, d, e, f;
	uint8_t channel;

	/* Extract address components */
	a = (uap_lap >> 23) & 0x1F;
	b = (uap_lap >> 19) & 0x0F;
	c = (uap_lap >> 15) & 0x10;
	d = (uap_lap >> 10) & 0x1FF;
	e = (uap_lap >> 5) & 0x1F;
	f = uap_lap & 0x1F;

	/* Clock phase */
	x = (clock >> 2) & 0x1F;

	/* XOR operations */
	y1 = (a ^ (clock >> 7)) & 0x1F;
	y2 = (b ^ (clock >> 2)) & 0x0F;

	/* Permutation */
	channel = perm_table[y2][y1 & 0x0F];
	channel = (channel + (y1 >> 4) * 16) % 79;

	/* Add offset */
	channel = (channel + f + (clock >> 7) * 16) % 79;

	return channel;
}

/*
 * AFH Channel Calculation
 * Core Spec Vol 2, Part B, Section 2.6.3
 */
uint8_t lll_bredr_afh_channel_calc(uint32_t clock, uint8_t *afh_map,
				   uint8_t afh_channel_count, uint32_t uap_lap)
{
	uint8_t channel;
	uint8_t mapped_channel;
	uint8_t count;

	/* Calculate base channel */
	channel = lll_bredr_hop_channel_calc(clock, NULL, 79, uap_lap);

	/* Check if channel is used */
	if (afh_map[channel / 8] & BIT(channel % 8)) {
		return channel;
	}

	/* Map to used channel */
	mapped_channel = channel % afh_channel_count;
	count = 0;

	for (int i = 0; i < 79; i++) {
		if (afh_map[i / 8] & BIT(i % 8)) {
			if (count == mapped_channel) {
				return i;
			}
			count++;
		}
	}

	/* Fallback */
	return channel;
}

/*
 * Access Code Generation
 * Core Spec Vol 2, Part B, Section 6.3
 */
void lll_bredr_access_code_gen(uint32_t lap, uint8_t *access_code)
{
	/* Access code is 72 bits:
	 * - Preamble: 4 bits
	 * - Sync word: 64 bits (derived from LAP)
	 * - Trailer: 4 bits
	 *
	 * For simplicity, we generate a basic access code here.
	 * Full implementation requires BCH encoding.
	 */
	uint64_t sync_word;

	/* Generate sync word from LAP using simplified algorithm */
	sync_word = lap;
	sync_word |= ((uint64_t)(lap ^ 0xFFFFFF) << 24);

	/* Pack into access code buffer (9 bytes = 72 bits) */
	access_code[0] = 0x55;  /* Preamble pattern */
	access_code[1] = (sync_word >> 0) & 0xFF;
	access_code[2] = (sync_word >> 8) & 0xFF;
	access_code[3] = (sync_word >> 16) & 0xFF;
	access_code[4] = (sync_word >> 24) & 0xFF;
	access_code[5] = (sync_word >> 32) & 0xFF;
	access_code[6] = (sync_word >> 40) & 0xFF;
	access_code[7] = (sync_word >> 48) & 0xFF;
	access_code[8] = (sync_word >> 56) & 0xFF;
}

void lll_bredr_dac_gen(uint8_t *bd_addr, uint8_t *access_code)
{
	uint32_t lap;

	/* Extract LAP from BD_ADDR */
	lap = bd_addr[0] | (bd_addr[1] << 8) | (bd_addr[2] << 16);

	lll_bredr_access_code_gen(lap, access_code);
}

void lll_bredr_cac_gen(uint8_t *bd_addr, uint8_t *access_code)
{
	/* CAC is same as DAC for the Central's BD_ADDR */
	lll_bredr_dac_gen(bd_addr, access_code);
}

/*
 * Inquiry Operations
 */

/* GIAC LAP: 0x9E8B33 */
#define GIAC_LAP 0x9E8B33

/* Inquiry hop frequencies (32 frequencies in A-train, 32 in B-train) */
static uint8_t inquiry_hop_calc(uint8_t train, uint8_t index, uint32_t clk)
{
	/* Simplified inquiry hopping - real implementation uses full algorithm */
	uint8_t base = (train == 0) ? 0 : 32;
	return (base + (index + (clk >> 1)) % 32) % 79;
}

static int inquiry_prepare_cb(struct lll_prepare_param *p)
{
	struct lll_bredr_inquiry *lll = p->param;
	uint8_t access_code[9];
	uint8_t hop_channel;
	uint32_t lap;

	LOG_DBG("Inquiry prepare: train=%u, n_inquiry=%u", lll->train, lll->n_inquiry);

	/* Get LAP for inquiry (GIAC or LIAC) */
	lap = lll->lap[0] | ((uint32_t)lll->lap[1] << 8) |
	      ((uint32_t)lll->lap[2] << 16);

	/* Generate IAC (Inquiry Access Code) */
	radio_bredr_iac_generate(lap, access_code);

	/* Calculate hop frequency for this inquiry slot */
	hop_channel = inquiry_hop_calc(lll->train, lll->hop_index,
				       ticker_ticks_now_get());

	/* Configure radio for inquiry TX */
	radio_bredr_freq_set(hop_channel);
	radio_bredr_access_code_set(access_code);
	radio_bredr_pkt_configure(RADIO_BREDR_PKT_NULL, 0);

	/* Set ISR for inquiry response */
	radio_bredr_isr_set(lll_bredr_inquiry_isr, lll);

	/* Enable TX for ID packet */
	radio_bredr_tx_enable();

	/* Update hop index */
	lll->hop_index = (lll->hop_index + 1) % 32;
	lll->hop_channel = hop_channel;

	return 0;
}

static void inquiry_abort_cb(struct lll_prepare_param *prepare_param,
			     void *param)
{
	int err;

	/* NOTE: This is not a prepare being cancelled */
	if (!prepare_param) {
		/* Perform event abort here.
		 * After event has been cleanly aborted, clean up resources
		 * and dispatch event done.
		 */
		radio_bredr_isr_set(NULL, NULL);
		radio_bredr_disable();
		LOG_DBG("Inquiry event aborted");
		return;
	}

	/* NOTE: Else clean the top half preparations of the aborted event
	 * currently in preparation pipeline.
	 */
	err = lll_hfclock_off();
	LL_ASSERT(err >= 0);

	lll_done(param);
}

static int inquiry_is_abort_cb(void *next, void *curr,
			       lll_prepare_cb_t *resume_cb)
{
	/* Inquiry has low priority, can be aborted by higher priority events */
	if (next != curr) {
		/* Yield to the pre-emptor */
		return -ECANCELED;
	}

	return -ECANCELED;
}

int lll_bredr_inquiry_start(struct lll_bredr_inquiry *inquiry)
{
	LOG_DBG("Inquiry start");
	return 0;
}

int lll_bredr_inquiry_stop(struct lll_bredr_inquiry *inquiry)
{
	LOG_DBG("Inquiry stop");
	return 0;
}

void lll_bredr_inquiry_prepare(void *param)
{
	struct lll_prepare_param *p = param;
	int err;

	/* Turn on HF clock */
	err = lll_hfclock_on();
	LL_ASSERT(err >= 0);

	/* Prepare the inquiry event */
	err = lll_prepare(inquiry_is_abort_cb, inquiry_abort_cb,
			  inquiry_prepare_cb, 0, param);
	LL_ASSERT(!err || err == -EINPROGRESS);
}

void lll_bredr_inquiry_isr(void *param)
{
	struct lll_bredr_inquiry *lll = param;
	uint32_t trx_done;

	/* Check if TX/RX completed */
	trx_done = radio_bredr_is_done();

	if (trx_done) {
		/* Check for FHS response */
		if (radio_bredr_crc_is_valid()) {
			/* FHS received - inquiry response */
			lll->current_responses++;
			LOG_DBG("Inquiry response received: count=%u",
				lll->current_responses);

			/* Check if we've reached max responses */
			if (lll->num_responses > 0 &&
			    lll->current_responses >= lll->num_responses) {
				/* Stop inquiry */
				radio_bredr_disable();
				return;
			}
		}
	}

	/* Continue with next inquiry slot */
	radio_bredr_disable();
}

/*
 * Inquiry Scan Operations
 */
static int inquiry_scan_prepare_cb(struct lll_prepare_param *p)
{
	struct lll_bredr_inquiry_scan *lll = p->param;
	uint8_t access_code[9];
	uint8_t hop_channel;
	uint32_t lap;

	LOG_DBG("Inquiry scan prepare");

	/* Use GIAC LAP for inquiry scan */
	lap = lll->lap[0] | ((uint32_t)lll->lap[1] << 8) |
	      ((uint32_t)lll->lap[2] << 16);

	/* Generate IAC */
	radio_bredr_iac_generate(lap, access_code);

	/* Calculate hop frequency */
	hop_channel = inquiry_hop_calc(0, 0, ticker_ticks_now_get());

	/* Configure radio for inquiry scan RX */
	radio_bredr_freq_set(hop_channel);
	radio_bredr_access_code_set(access_code);
	radio_bredr_pkt_configure(RADIO_BREDR_PKT_NULL, 0);

	/* Set ISR */
	radio_bredr_isr_set(lll_bredr_inquiry_scan_isr, lll);

	/* Enable RX */
	radio_bredr_rx_enable();

	return 0;
}

static void inquiry_scan_abort_cb(struct lll_prepare_param *prepare_param,
				  void *param)
{
	int err;

	/* NOTE: This is not a prepare being cancelled */
	if (!prepare_param) {
		/* Perform event abort here */
		radio_bredr_isr_set(NULL, NULL);
		radio_bredr_disable();
		LOG_DBG("Inquiry scan event aborted");
		return;
	}

	/* Clean the top half preparations of the aborted event */
	err = lll_hfclock_off();
	LL_ASSERT(err >= 0);

	lll_done(param);
}

static int inquiry_scan_is_abort_cb(void *next, void *curr,
				    lll_prepare_cb_t *resume_cb)
{
	/* Inquiry scan has low priority, can be aborted */
	if (next != curr) {
		return -ECANCELED;
	}

	return -ECANCELED;
}

int lll_bredr_inquiry_scan_start(struct lll_bredr_inquiry_scan *scan)
{
	LOG_DBG("Inquiry scan start");
	return 0;
}

int lll_bredr_inquiry_scan_stop(struct lll_bredr_inquiry_scan *scan)
{
	LOG_DBG("Inquiry scan stop");
	return 0;
}

void lll_bredr_inquiry_scan_prepare(void *param)
{
	int err;

	err = lll_hfclock_on();
	LL_ASSERT(err >= 0);

	err = lll_prepare(inquiry_scan_is_abort_cb, inquiry_scan_abort_cb,
			  inquiry_scan_prepare_cb, 0, param);
	LL_ASSERT(!err || err == -EINPROGRESS);
}

void lll_bredr_inquiry_scan_isr(void *param)
{
	struct lll_bredr_inquiry_scan *lll = param;
	uint32_t trx_done;

	/* Check if RX completed */
	trx_done = radio_bredr_is_done();

	if (trx_done) {
		/* Check for valid ID packet */
		if (radio_bredr_crc_is_valid()) {
			/* ID packet received - respond with FHS */
			lll->fhs_pending = 1;
			LOG_DBG("Inquiry ID received, FHS pending");
		}
	}

	radio_bredr_disable();
}

/*
 * Page Operations
 */

/* Page hop frequency calculation */
static uint8_t page_hop_calc(uint8_t train, uint8_t index, uint32_t clk,
			     uint16_t clock_offset)
{
	/* Simplified page hopping - real implementation uses full algorithm */
	uint32_t adjusted_clk = clk + clock_offset;
	uint8_t base = (train == 0) ? 0 : 16;
	return (base + (index + (adjusted_clk >> 1)) % 16) % 79;
}

static int page_prepare_cb(struct lll_prepare_param *p)
{
	struct lll_bredr_page *lll = p->param;
	uint8_t access_code[9];
	uint8_t hop_channel;

	LOG_DBG("Page prepare: train=%u, n_page=%u", lll->train, lll->n_page);

	/* Generate DAC (Device Access Code) from target BD_ADDR */
	radio_bredr_dac_generate(lll->bd_addr, access_code);

	/* Calculate hop frequency using clock offset hint */
	hop_channel = page_hop_calc(lll->train, lll->hop_index,
				    ticker_ticks_now_get(), lll->clock_offset);

	/* Configure radio for page TX */
	radio_bredr_freq_set(hop_channel);
	radio_bredr_access_code_set(access_code);
	radio_bredr_pkt_configure(RADIO_BREDR_PKT_NULL, 0);

	/* Set ISR */
	radio_bredr_isr_set(lll_bredr_page_isr, lll);

	/* Enable TX for ID packet */
	radio_bredr_tx_enable();

	/* Update hop index */
	lll->hop_index = (lll->hop_index + 1) % 16;
	lll->hop_channel = hop_channel;

	return 0;
}

static void page_abort_cb(struct lll_prepare_param *prepare_param,
			  void *param)
{
	int err;

	/* NOTE: This is not a prepare being cancelled */
	if (!prepare_param) {
		/* Perform event abort here */
		radio_bredr_isr_set(NULL, NULL);
		radio_bredr_disable();
		LOG_DBG("Page event aborted");
		return;
	}

	/* Clean the top half preparations of the aborted event */
	err = lll_hfclock_off();
	LL_ASSERT(err >= 0);

	lll_done(param);
}

static int page_is_abort_cb(void *next, void *curr,
			    lll_prepare_cb_t *resume_cb)
{
	/* Page has medium priority, can be aborted by SCO/eSCO */
	if (next != curr) {
		return -ECANCELED;
	}

	return -ECANCELED;
}

int lll_bredr_page_start(struct lll_bredr_page *page)
{
	LOG_DBG("Page start");
	return 0;
}

int lll_bredr_page_stop(struct lll_bredr_page *page)
{
	LOG_DBG("Page stop");
	return 0;
}

void lll_bredr_page_prepare(void *param)
{
	int err;

	err = lll_hfclock_on();
	LL_ASSERT(err >= 0);

	err = lll_prepare(page_is_abort_cb, page_abort_cb,
			  page_prepare_cb, 0, param);
	LL_ASSERT(!err || err == -EINPROGRESS);
}

void lll_bredr_page_isr(void *param)
{
	struct lll_bredr_page *lll = param;
	uint32_t trx_done;

	/* Check if TX/RX completed */
	trx_done = radio_bredr_is_done();

	if (trx_done) {
		/* Check for ID response from peripheral */
		if (radio_bredr_crc_is_valid()) {
			/* ID response received - send FHS */
			LOG_DBG("Page response received");
			/* State machine will handle FHS exchange */
		}
	}

	radio_bredr_disable();
}

/*
 * Page Scan Operations
 */
static int page_scan_prepare_cb(struct lll_prepare_param *p)
{
	struct lll_bredr_page_scan *lll = p->param;
	uint8_t access_code[9];
	uint8_t hop_channel;

	LOG_DBG("Page scan prepare");

	/* Generate DAC from local BD_ADDR */
	/* Note: In page scan, we listen for our own DAC */
	radio_bredr_dac_generate((uint8_t *)"\x00\x00\x00\x00\x00\x00", access_code);

	/* Calculate hop frequency */
	hop_channel = page_hop_calc(0, 0, ticker_ticks_now_get(), 0);

	/* Configure radio for page scan RX */
	radio_bredr_freq_set(hop_channel);
	radio_bredr_access_code_set(access_code);
	radio_bredr_pkt_configure(RADIO_BREDR_PKT_NULL, 0);

	/* Set ISR */
	radio_bredr_isr_set(lll_bredr_page_scan_isr, lll);

	/* Enable RX */
	radio_bredr_rx_enable();
	return 0;
}

static void page_scan_abort_cb(struct lll_prepare_param *prepare_param,
			       void *param)
{
	int err;

	/* NOTE: This is not a prepare being cancelled */
	if (!prepare_param) {
		/* Perform event abort here */
		radio_bredr_isr_set(NULL, NULL);
		radio_bredr_disable();
		LOG_DBG("Page scan event aborted");
		return;
	}

	/* Clean the top half preparations of the aborted event */
	err = lll_hfclock_off();
	LL_ASSERT(err >= 0);

	lll_done(param);
}

static int page_scan_is_abort_cb(void *next, void *curr,
				 lll_prepare_cb_t *resume_cb)
{
	/* Page scan has low priority, can be aborted */
	if (next != curr) {
		return -ECANCELED;
	}

	return -ECANCELED;
}

int lll_bredr_page_scan_start(struct lll_bredr_page_scan *scan)
{
	LOG_DBG("Page scan start");
	return 0;
}

int lll_bredr_page_scan_stop(struct lll_bredr_page_scan *scan)
{
	LOG_DBG("Page scan stop");
	return 0;
}

void lll_bredr_page_scan_prepare(void *param)
{
	int err;

	err = lll_hfclock_on();
	LL_ASSERT(err >= 0);

	err = lll_prepare(page_scan_is_abort_cb, page_scan_abort_cb,
			  page_scan_prepare_cb, 0, param);
	LL_ASSERT(!err || err == -EINPROGRESS);
}

void lll_bredr_page_scan_isr(void *param)
{
	struct lll_bredr_page_scan *lll = param;
	uint32_t trx_done;

	/* Check if RX completed */
	trx_done = radio_bredr_is_done();

	if (trx_done) {
		/* Check for valid ID packet (page request) */
		if (radio_bredr_crc_is_valid()) {
			/* ID packet received - respond with ID and prepare for FHS */
			lll->id_received = 1;
			LOG_DBG("Page ID received, responding");

			/* Switch to TX mode to send ID response */
			radio_bredr_tx_enable();
		}
	}

	radio_bredr_disable();
}

/*
 * ACL Connection Operations
 */

/* Connection TX state */
static struct {
	struct lmp_tx_node *lmp_tx;     /* Current LMP TX node */
	uint8_t empty;                   /* Empty PDU flag */
	uint8_t tx_pdu[MAX_BREDR_PDU_SIZE]; /* TX PDU buffer */
	uint16_t tx_len;                 /* TX PDU length (can exceed 255 for BR/EDR) */
} conn_tx_state;

/**
 * @brief Prepare TX PDU for connection event
 *
 * This function prepares the next PDU to transmit, prioritizing:
 * 1. LMP PDUs (control plane)
 * 2. ACL data PDUs (data plane)
 * 3. Empty PDU (if nothing to send)
 *
 * Similar to BLE's lll_conn_pdu_tx_prep()
 */
static void conn_tx_pdu_prep(struct lll_bredr_conn *lll,
			     struct ull_bredr_conn *conn,
			     uint8_t **pdu, uint16_t *len)
{
	struct lmp_tx_node *tx_node;

	/* Check for pending LMP PDU first (higher priority) */
	tx_node = ull_bredr_lmp_tx_dequeue(conn);
	if (tx_node != NULL) {
		/* LMP PDU ready for transmission */
		conn_tx_state.lmp_tx = tx_node;
		conn_tx_state.empty = 0;

		/* Copy LMP PDU to TX buffer */
		memcpy(conn_tx_state.tx_pdu, tx_node->pdu, tx_node->len);
		conn_tx_state.tx_len = tx_node->len;

		*pdu = conn_tx_state.tx_pdu;
		*len = conn_tx_state.tx_len;

		LOG_DBG("LMP TX prep: opcode=0x%02x, len=%u",
			tx_node->pdu[0], tx_node->len);
		return;
	}

	/* Check for pending ACL data */
	if (lll->memq_tx.head != lll->memq_tx.tail) {
		/* ACL data available - dequeue from TX queue
		 * Similar to BLE's memq_peek for ACL data
		 */
		void *tx_node;

		tx_node = lll->memq_tx.head;
		if (tx_node) {
			/* ACL data PDU ready for transmission */
			conn_tx_state.lmp_tx = NULL;
			conn_tx_state.empty = 0;

			/* Get PDU from TX node - the node structure contains
			 * the PDU data after the link pointer
			 */
			*pdu = (uint8_t *)tx_node + sizeof(void *);
			*len = MAX_BREDR_PDU_SIZE;  /* Will be set by actual packet */

			LOG_DBG("ACL TX prep: node=%p", tx_node);
			return;
		}

		conn_tx_state.lmp_tx = NULL;
		conn_tx_state.empty = 0;

		/* Fallback to empty if dequeue failed */
		conn_tx_state.empty = 1;
		*pdu = NULL;
		*len = 0;
		return;
	}

	/* No data to send - prepare empty PDU (POLL/NULL) */
	conn_tx_state.lmp_tx = NULL;
	conn_tx_state.empty = 1;
	*pdu = NULL;
	*len = 0;
}

/**
 * @brief Calculate hop frequency for current slot
 *
 * BR/EDR uses frequency hopping based on the Bluetooth clock.
 * For AFH-enabled connections, only channels in the AFH map are used.
 */
static uint8_t conn_hop_freq_calc(struct lll_bredr_conn *lll, uint32_t clock)
{
	uint32_t uap_lap;
	uint8_t channel;

	/* Extract UAP and LAP from BD_ADDR for hopping calculation */
	uap_lap = lll->bd_addr[0] |
		  ((uint32_t)lll->bd_addr[1] << 8) |
		  ((uint32_t)lll->bd_addr[2] << 16) |
		  ((uint32_t)lll->bd_addr[3] << 24);

	if (lll->afh_enabled && lll->afh_channel_count > 0) {
		/* AFH enabled - use AFH channel calculation */
		channel = lll_bredr_afh_channel_calc(clock, lll->afh_channel_map,
						     lll->afh_channel_count,
						     uap_lap);
	} else {
		/* Standard hopping - use all 79 channels */
		channel = lll_bredr_hop_channel_calc(clock, NULL, 79, uap_lap);
	}

	return channel;
}

/**
 * @brief Connection prepare callback
 *
 * Called by LLL to prepare for a connection event.
 * This is the BR/EDR equivalent of BLE's connection prepare.
 *
 * Responsibilities:
 * 1. Run LMP state machines for pending procedures
 * 2. Prepare TX PDU (LMP or ACL data)
 * 3. Calculate hop frequency
 * 4. Configure radio parameters
 */
static int conn_prepare_cb(struct lll_prepare_param *p)
{
	struct lll_bredr_conn *lll = p->param;
	struct ull_bredr_conn *conn;
	uint8_t *tx_pdu;
	uint16_t tx_len;
	uint8_t hop_channel;
	uint8_t access_code[9];
	uint32_t clock;

	LOG_DBG("Connection prepare: handle=%u, role=%s",
		lll->handle, lll->role ? "peripheral" : "central");

	/* Get ULL connection context */
	conn = CONTAINER_OF(lll, struct ull_bredr_conn, lll);

	/* Run LMP state machines for pending procedures
	 * This is similar to BLE's ull_cp_run() which runs:
	 * - llcp_rr_run() for remote request FSM
	 * - llcp_lr_run() for local request FSM
	 *
	 * For BR/EDR, we run lmp_run() to process pending LMP procedures.
	 */
	lmp_run(conn);

	/* Prepare TX PDU - prioritizes LMP over ACL data */
	conn_tx_pdu_prep(lll, conn, &tx_pdu, &tx_len);

	/* Get current Bluetooth clock (28-bit, wraps at 2^28) */
	clock = ticker_ticks_now_get();
	clock = (clock >> 1) & LLL_BREDR_CLOCK_WRAP;  /* Convert to BT clock */

	/* Calculate hop frequency for this slot */
	hop_channel = conn_hop_freq_calc(lll, clock);

	/* Generate Channel Access Code (CAC) from Central's BD_ADDR */
	if (lll->role == 0) {
		/* Central - use local BD_ADDR for CAC
		 * Get local BD_ADDR from connection info
		 */
		lll_bredr_access_code_gen(
			conn->info.local_bd_addr[0] |
			((uint32_t)conn->info.local_bd_addr[1] << 8) |
			((uint32_t)conn->info.local_bd_addr[2] << 16),
			access_code);
	} else {
		/* Peripheral - use Central's BD_ADDR for CAC */
		lll_bredr_access_code_gen(
			lll->bd_addr[0] |
			((uint32_t)lll->bd_addr[1] << 8) |
			((uint32_t)lll->bd_addr[2] << 16),
			access_code);
	}

	/* Configure radio for this connection event */

	/* 1. Set frequency */
	radio_bredr_freq_set(hop_channel);

	/* 2. Set access code */
	radio_bredr_access_code_set(access_code);

	/* 3. Configure packet type based on max slots */
	radio_bredr_pkt_configure(lll->packet_type, lll->max_slots);

	/* 4. Set TX buffer if we have data */
	if (tx_pdu && tx_len > 0) {
		radio_bredr_pkt_tx_set(tx_pdu);
	} else {
		/* Use empty packet (NULL/POLL) */
		radio_bredr_pkt_tx_set(radio_bredr_pkt_empty_get());
	}

	/* 5. Set RX buffer */
	radio_bredr_pkt_rx_set(radio_bredr_pkt_scratch_get());

	/* 6. Set up radio ISR */
	radio_bredr_isr_set(lll_bredr_conn_isr, lll);

	/* 7. Start radio based on role */
	if (lll->role == 0) {
		/* Central transmits first in slot */
		radio_bredr_tx_enable();
	} else {
		/* Peripheral receives first in slot */
		radio_bredr_rx_enable();
	}

	LOG_DBG("Conn event: ch=%u, clock=0x%08x, tx=%s",
		hop_channel, clock,
		conn_tx_state.empty ? "empty" : "data");

	/* Update statistics */
	lll->tx_count++;

	return 0;
}

/**
 * @brief Handle TX acknowledgment
 *
 * Called when a transmitted PDU has been acknowledged.
 * Releases the TX node back to the pool.
 */
static void conn_tx_ack(struct lll_bredr_conn *lll)
{
	if (conn_tx_state.lmp_tx != NULL) {
		/* Release LMP TX node after acknowledgment */
		ull_bredr_lmp_tx_release(conn_tx_state.lmp_tx);
		conn_tx_state.lmp_tx = NULL;

		LOG_DBG("LMP TX ack: released");
	}

	/* Handle ACL data TX acknowledgment */
	/* Dequeue acknowledged ACL data from TX queue */
	if (lll->memq_tx.head != lll->memq_tx.tail) {
		void *tx_node;

		/* Dequeue the head node */
		tx_node = lll->memq_tx.head;
		if (tx_node) {
			/* Move head to next node */
			lll->memq_tx.head = *((void **)tx_node);
			/* The TX node will be released by ULL layer */
			LOG_DBG("ACL TX ack: node=%p released", tx_node);
		}
	}
}

static void conn_abort_cb(struct lll_prepare_param *prepare_param,
			  void *param)
{
	int err;

	/* NOTE: This is not a prepare being cancelled */
	if (!prepare_param) {
		/* Perform event abort here.
		 * ACL connection event can be aborted by SCO/eSCO.
		 */
		radio_bredr_isr_set(NULL, NULL);
		radio_bredr_disable();
		LOG_DBG("Connection event aborted");
		return;
	}

	/* Clean the top half preparations of the aborted event */
	err = lll_hfclock_off();
	LL_ASSERT(err >= 0);

	lll_done(param);
}

static int conn_is_abort_cb(void *next, void *curr,
			    lll_prepare_cb_t *resume_cb)
{
	/* ACL connections have lower priority than SCO/eSCO
	 * but higher than inquiry/page scan
	 */
	if (next != curr) {
		/* Check if pre-emptor is SCO/eSCO (higher priority) */
		/* For now, yield to any pre-emptor */
		return -ECANCELED;
	}

	/* Continue with current event */
	return 0;
}

int lll_bredr_conn_start(struct lll_bredr_conn *conn)
{
	LOG_DBG("Connection start: handle=%u", conn->handle);
	return 0;
}

int lll_bredr_conn_stop(struct lll_bredr_conn *conn)
{
	LOG_DBG("Connection stop: handle=%u", conn->handle);
	return 0;
}

void lll_bredr_conn_prepare(void *param)
{
	int err;

	err = lll_hfclock_on();
	LL_ASSERT(err >= 0);

	err = lll_prepare(conn_is_abort_cb, conn_abort_cb,
			  conn_prepare_cb, 0, param);
	LL_ASSERT(!err || err == -EINPROGRESS);
}

void lll_bredr_conn_isr(void *param)
{
	struct lll_bredr_conn *lll = param;
	struct ull_bredr_conn *conn;
	uint32_t trx_done;
	uint8_t rx_buffer[MAX_BREDR_PDU_SIZE];

	/* Get ULL connection context */
	conn = CONTAINER_OF(lll, struct ull_bredr_conn, lll);

	/* Check if TX/RX completed */
	trx_done = radio_bredr_is_done();

	if (trx_done) {
		/* Check CRC validity */
		if (radio_bredr_crc_is_valid()) {
			/* Valid packet received */

			/* Check ARQN in received packet header for TX acknowledgment */
			/* If our last TX was acknowledged, release the TX buffer */
			if (lll->tx_arqn) {
				conn_tx_ack(lll);
			}

			/* Get received packet */
			uint8_t *rx_pdu = rx_buffer;
			uint8_t rx_len = 0;

			/* Check packet type from header */
			/* For LMP PDU (L_CH = 11), process via LMP handler */
			/* For ACL data (L_CH = 10 or 01), enqueue to RX queue */

			/* Simplified: assume LMP PDU for now */
			if (rx_len > 0 && rx_len <= MAX_LMP_PDU_SIZE) {
				/* LMP PDU received - process it */
				ull_bredr_lmp_rx(conn, rx_pdu, rx_len);
			}

			/* Update flow control state */
			lll->rx_seqn = !lll->rx_seqn;
			lll->tx_arqn = 1;  /* ACK the received packet */
		} else {
			/* CRC error - NAK */
			lll->tx_arqn = 0;
		}
	} else {
		/* Timeout - no response received */
		lll->tx_arqn = 0;
	}

	/* Update RX statistics */
	lll->rx_count++;

	/* Disable radio for this slot */
	radio_bredr_disable();

	LOG_DBG("Conn ISR: handle=%u, rx_count=%u", lll->handle, lll->rx_count);
}

/*
 * SCO Operations
 */
static int sco_prepare_cb(struct lll_prepare_param *p)
{
	struct lll_bredr_sco *lll = p->param;
	uint8_t access_code[9];
	uint8_t hop_channel;
	uint32_t clock;

	LOG_DBG("SCO prepare: handle=%u", lll->handle);

	/* Get current Bluetooth clock */
	clock = ticker_ticks_now_get();
	clock = (clock >> 1) & LLL_BREDR_CLOCK_WRAP;

	/* Calculate hop frequency for SCO slot */
	hop_channel = lll_bredr_hop_channel_calc(clock, NULL, 79,
		lll->bd_addr[0] |
		((uint32_t)lll->bd_addr[1] << 8) |
		((uint32_t)lll->bd_addr[2] << 16) |
		((uint32_t)lll->bd_addr[3] << 24));

	/* Generate access code */
	lll_bredr_access_code_gen(
		lll->bd_addr[0] |
		((uint32_t)lll->bd_addr[1] << 8) |
		((uint32_t)lll->bd_addr[2] << 16),
		access_code);

	/* Configure radio for SCO */
	radio_bredr_freq_set(hop_channel);
	radio_bredr_access_code_set(access_code);
	radio_bredr_pkt_configure(lll->packet_type, lll->voice_setting);

	/* Set ISR */
	radio_bredr_isr_set(lll_bredr_sco_isr, lll);

	/* SCO timing is symmetric - Central TX, Peripheral RX in same slot */
	if (lll->role == 0) {
		/* Central transmits first */
		radio_bredr_tx_enable();
	} else {
		/* Peripheral receives first */
		radio_bredr_rx_enable();
	}

	return 0;
}

static void sco_abort_cb(struct lll_prepare_param *prepare_param,
			 void *param)
{
	int err;

	/* NOTE: This is not a prepare being cancelled */
	if (!prepare_param) {
		/* SCO should not be aborted - it's time-critical
		 * If we get here, voice quality will be affected
		 */
		LOG_WRN("SCO event aborted - voice quality affected");
		radio_bredr_isr_set(NULL, NULL);
		radio_bredr_disable();
		return;
	}

	/* Clean the top half preparations of the aborted event */
	err = lll_hfclock_off();
	LL_ASSERT(err >= 0);

	lll_done(param);
}

static int sco_is_abort_cb(void *next, void *curr,
			   lll_prepare_cb_t *resume_cb)
{
	/* SCO has highest priority, should not be aborted by other events.
	 * Return 0 to indicate we should NOT be aborted.
	 */
	if (next != curr) {
		/* Do not yield - SCO is time-critical */
		return 0;
	}

	/* Continue with current event */
	return 0;
}

int lll_bredr_sco_start(struct lll_bredr_sco *sco)
{
	LOG_DBG("SCO start: handle=%u", sco->handle);
	return 0;
}

int lll_bredr_sco_stop(struct lll_bredr_sco *sco)
{
	LOG_DBG("SCO stop: handle=%u", sco->handle);
	return 0;
}

void lll_bredr_sco_prepare(void *param)
{
	int err;

	err = lll_hfclock_on();
	LL_ASSERT(err >= 0);

	err = lll_prepare(sco_is_abort_cb, sco_abort_cb,
			  sco_prepare_cb, 0, param);
	LL_ASSERT(!err || err == -EINPROGRESS);
}

void lll_bredr_sco_isr(void *param)
{
	struct lll_bredr_sco *lll = param;
	uint32_t trx_done;

	/* Check if TX/RX completed */
	trx_done = radio_bredr_is_done();

	if (trx_done) {
		/* SCO uses synchronous timing - no retransmission */
		if (radio_bredr_crc_is_valid()) {
			/* Valid voice data received */
			lll->rx_count++;
			LOG_DBG("SCO RX: handle=%u", lll->handle);
		} else {
			/* CRC error - voice data lost */
			lll->err_count++;
		}
	}

	/* Disable radio */
	radio_bredr_disable();
}

/*
 * eSCO Operations
 */
static int esco_prepare_cb(struct lll_prepare_param *p)
{
	struct lll_bredr_esco *lll = p->param;
	uint8_t access_code[9];
	uint8_t hop_channel;
	uint32_t clock;

	LOG_DBG("eSCO prepare: handle=%u", lll->handle);

	/* Get current Bluetooth clock */
	clock = ticker_ticks_now_get();
	clock = (clock >> 1) & LLL_BREDR_CLOCK_WRAP;

	/* Calculate hop frequency for eSCO slot */
	hop_channel = lll_bredr_hop_channel_calc(clock, NULL, 79,
		lll->bd_addr[0] |
		((uint32_t)lll->bd_addr[1] << 8) |
		((uint32_t)lll->bd_addr[2] << 16) |
		((uint32_t)lll->bd_addr[3] << 24));

	/* Generate access code */
	lll_bredr_access_code_gen(
		lll->bd_addr[0] |
		((uint32_t)lll->bd_addr[1] << 8) |
		((uint32_t)lll->bd_addr[2] << 16),
		access_code);

	/* Configure radio for eSCO */
	radio_bredr_freq_set(hop_channel);
	radio_bredr_access_code_set(access_code);
	radio_bredr_pkt_configure(lll->packet_type, lll->voice_setting);

	/* Set ISR */
	radio_bredr_isr_set(lll_bredr_esco_isr, lll);

	/* eSCO timing - Central TX, Peripheral RX */
	if (lll->role == 0) {
		/* Central transmits first */
		radio_bredr_tx_enable();
	} else {
		/* Peripheral receives first */
		radio_bredr_rx_enable();
	}

	return 0;
}

static void esco_abort_cb(struct lll_prepare_param *prepare_param,
			  void *param)
{
	int err;

	/* NOTE: This is not a prepare being cancelled */
	if (!prepare_param) {
		/* eSCO should not be aborted - it's time-critical
		 * If we get here, voice quality will be affected
		 */
		LOG_WRN("eSCO event aborted - voice quality affected");
		radio_bredr_isr_set(NULL, NULL);
		radio_bredr_disable();
		return;
	}

	/* Clean the top half preparations of the aborted event */
	err = lll_hfclock_off();
	LL_ASSERT(err >= 0);

	lll_done(param);
}

static int esco_is_abort_cb(void *next, void *curr,
			    lll_prepare_cb_t *resume_cb)
{
	/* eSCO has highest priority, should not be aborted by other events.
	 * Return 0 to indicate we should NOT be aborted.
	 */
	if (next != curr) {
		/* Do not yield - eSCO is time-critical */
		return 0;
	}

	/* Continue with current event */
	return 0;
}

int lll_bredr_esco_start(struct lll_bredr_esco *esco)
{
	LOG_DBG("eSCO start: handle=%u", esco->handle);
	return 0;
}

int lll_bredr_esco_stop(struct lll_bredr_esco *esco)
{
	LOG_DBG("eSCO stop: handle=%u", esco->handle);
	return 0;
}

void lll_bredr_esco_prepare(void *param)
{
	int err;

	err = lll_hfclock_on();
	LL_ASSERT(err >= 0);

	err = lll_prepare(esco_is_abort_cb, esco_abort_cb,
			  esco_prepare_cb, 0, param);
	LL_ASSERT(!err || err == -EINPROGRESS);
}

void lll_bredr_esco_isr(void *param)
{
	struct lll_bredr_esco *lll = param;
	uint32_t trx_done;

	/* Check if TX/RX completed */
	trx_done = radio_bredr_is_done();

	if (trx_done) {
		if (radio_bredr_crc_is_valid()) {
			/* Valid voice data received */
			lll->rx_count++;
			lll->retx_count = 0;  /* Reset retransmission counter */
			LOG_DBG("eSCO RX: handle=%u", lll->handle);
		} else {
			/* CRC error - may retransmit in retransmission window */
			lll->err_count++;
			if (lll->retx_count < lll->retx_effort) {
				lll->retx_count++;
				/* Stay in RX mode for retransmission */
				radio_bredr_rx_enable();
				return;
			}
		}
	}

	/* Disable radio */
	radio_bredr_disable();
}

/*
 * Encryption
 */
int lll_bredr_encrypt_start(struct lll_bredr_conn *conn, uint8_t *key,
			    uint8_t key_size)
{
	memcpy(conn->encryption_key, key, key_size);
	conn->encryption_key_size = key_size;
	conn->encryption_mode = BREDR_ENCRYPTION_E0;

	LOG_DBG("Encryption started: key_size=%u", key_size);
	return 0;
}

int lll_bredr_encrypt_stop(struct lll_bredr_conn *conn)
{
	conn->encryption_mode = BREDR_ENCRYPTION_OFF;
	memset(conn->encryption_key, 0, sizeof(conn->encryption_key));

	LOG_DBG("Encryption stopped");
	return 0;
}
