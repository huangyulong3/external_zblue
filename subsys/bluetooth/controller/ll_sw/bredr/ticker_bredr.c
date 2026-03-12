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
 * BR/EDR Ticker Integration Implementation
 * Provides BR/EDR activity scheduling using Zephyr's ticker infrastructure
 * for BLE/BR/EDR coexistence
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/hci_types.h>

#include "hal/ticker.h"

#include "util/util.h"
#include "util/mem.h"
#include "util/memq.h"
#include "util/mayfly.h"

#include "ticker/ticker.h"

#include "lll.h"
#include "lll_bredr.h"
#include "ull_bredr.h"
#include "ticker_bredr.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_ctlr_ticker_bredr, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

/*
 * BR/EDR Coexistence Parameters
 * These are computed dynamically based on active activities
 */
static struct {
	/* Default scan event duration (in us) */
	uint32_t scan_evt_dur;
	/* ACL event duration (in us) */
	uint32_t acl_evt_dur;
	/* Active BR/EDR activities bitmap */
	uint8_t active_modes;
	/* SCO/eSCO minimum interval (in slots) */
	uint8_t sco_intv_min;
	/* Number of active SCO links */
	uint8_t sco_count;
} bredr_slice_params;

/* Default durations */
#define BREDR_SCAN_EVT_DUR_DEFAULT_US  (18 * TICKER_BREDR_SLOT_US)  /* 18 slots */
#define BREDR_ACL_EVT_DUR_DEFAULT_US   (15 * TICKER_BREDR_SLOT_US)  /* 15 slots */

/*
 * Ticker operation callback - simple version for non-blocking operations
 */
static void ticker_op_cb(uint32_t status, void *param)
{
	ARG_UNUSED(param);

	if (status != TICKER_STATUS_SUCCESS &&
	    status != TICKER_STATUS_BUSY) {
		LOG_WRN("Ticker operation failed: %u", status);
	}
}

/*
 * Ticker start operation callback with semaphore signaling
 * Used for synchronous ticker operations from thread context
 */
static void ticker_op_start_cb(uint32_t status, void *param)
{
	uint32_t *ret_cb = param;

	if (ret_cb) {
		*ret_cb = status;
	}
}

/*
 * Coexistence parameter computation
 * Computes optimal event durations based on active activities
 */
static void ticker_bredr_slice_compute(void)
{
	/* Start with default values */
	bredr_slice_params.scan_evt_dur = BREDR_SCAN_EVT_DUR_DEFAULT_US;
	bredr_slice_params.acl_evt_dur = BREDR_ACL_EVT_DUR_DEFAULT_US;

	/* Adjust based on SCO presence */
	if (bredr_slice_params.sco_count > 0) {
		uint8_t t_sco = bredr_slice_params.sco_intv_min;

		if (t_sco == 6) {
			/* Tesco = 6 slots (HV3/EV3) - tight timing */
			bredr_slice_params.scan_evt_dur = 6 * TICKER_BREDR_SLOT_US;
			bredr_slice_params.acl_evt_dur = 3 * TICKER_BREDR_SLOT_US;
		} else if (t_sco == 12) {
			/* Tesco = 12 slots (2-EV3) */
			bredr_slice_params.scan_evt_dur = 16 * TICKER_BREDR_SLOT_US;
		}
	}
}

/*
 * Utility Functions
 */
uint32_t ticker_bredr_slots_to_ticks(uint16_t slots)
{
	/* Convert BR/EDR slots (625us) to ticker ticks */
	return HAL_TICKER_US_TO_TICKS((uint32_t)slots * TICKER_BREDR_SLOT_US);
}

uint16_t ticker_bredr_ticks_to_slots(uint32_t ticks)
{
	uint32_t us = HAL_TICKER_TICKS_TO_US(ticks);
	return (uint16_t)(us / TICKER_BREDR_SLOT_US);
}

uint32_t ticker_bredr_us_to_ticks(uint32_t us)
{
	return HAL_TICKER_US_TO_TICKS(us);
}

/*
 * Coexistence helper functions
 */
uint32_t ticker_bredr_scan_evt_dur_get(void)
{
	return bredr_slice_params.scan_evt_dur;
}

uint32_t ticker_bredr_acl_evt_dur_get(void)
{
	return bredr_slice_params.acl_evt_dur;
}

/*
 * Initialization
 */
int ticker_bredr_init(void)
{
	/* Initialize coexistence parameters */
	bredr_slice_params.scan_evt_dur = BREDR_SCAN_EVT_DUR_DEFAULT_US;
	bredr_slice_params.acl_evt_dur = BREDR_ACL_EVT_DUR_DEFAULT_US;
	bredr_slice_params.active_modes = 0;
	bredr_slice_params.sco_intv_min = 0xFF;
	bredr_slice_params.sco_count = 0;

	LOG_DBG("BR/EDR ticker initialized");
	return 0;
}

int ticker_bredr_reset(void)
{
	/* Stop all BR/EDR tickers */
	ticker_bredr_inquiry_stop();
	ticker_bredr_inquiry_scan_stop();
	ticker_bredr_page_stop();
	ticker_bredr_page_scan_stop();

	for (int i = 0; i < ULL_BREDR_MAX_CONN; i++) {
		ticker_bredr_conn_stop(i);
	}

	for (int i = 0; i < ULL_BREDR_SCO_MAX; i++) {
		ticker_bredr_sco_stop(i);
	}

	for (int i = 0; i < ULL_BREDR_ESCO_MAX; i++) {
		ticker_bredr_esco_stop(i);
	}

	/* Reset coexistence parameters */
	bredr_slice_params.active_modes = 0;
	bredr_slice_params.sco_intv_min = 0xFF;
	bredr_slice_params.sco_count = 0;
	ticker_bredr_slice_compute();

	LOG_DBG("BR/EDR ticker reset");
	return 0;
}

/*
 * Inquiry Ticker
 */
int ticker_bredr_inquiry_start(void *param, uint32_t ticks_anchor,
			       uint32_t ticks_slot)
{
	struct ull_bredr_inquiry *inquiry = param;
	uint32_t ticks_interval;
	uint32_t volatile ret_cb;
	uint8_t ret;

	/* Use computed scan duration */
	ticks_slot = ticker_bredr_us_to_ticks(bredr_slice_params.scan_evt_dur);
	ticks_interval = ticker_bredr_us_to_ticks(TICKER_BREDR_INQUIRY_INTERVAL_US);

	ret_cb = TICKER_STATUS_BUSY;
	ret = ticker_start(TICKER_INSTANCE_ID_CTLR,
			   TICKER_USER_ID_THREAD,
			   TICKER_ID_BREDR_INQUIRY,
			   ticks_anchor,
			   ticks_interval,
			   ticks_interval,
			   TICKER_NULL_REMAINDER,
			   TICKER_NULL_LAZY,
			   ticks_slot,
			   ticker_bredr_inquiry_cb,
			   inquiry,
			   ticker_op_start_cb,
			   (void *)&ret_cb);

	if (ret == TICKER_STATUS_BUSY) {
		/* Wait for callback - in real implementation use semaphore */
		while (ret_cb == TICKER_STATUS_BUSY) {
			k_yield();
		}
		ret = ret_cb;
	}

	if (ret != TICKER_STATUS_SUCCESS) {
		LOG_ERR("Inquiry ticker start failed: %u", ret);
		return -EIO;
	}

	LOG_DBG("Inquiry ticker started");
	return 0;
}

int ticker_bredr_inquiry_stop(void)
{
	uint8_t ret;

	ret = ticker_stop(TICKER_INSTANCE_ID_CTLR,
			  TICKER_USER_ID_THREAD,
			  TICKER_ID_BREDR_INQUIRY,
			  ticker_op_cb,
			  NULL);

	if (ret != TICKER_STATUS_SUCCESS && ret != TICKER_STATUS_BUSY) {
		return -EIO;
	}

	LOG_DBG("Inquiry ticker stopped");
	return 0;
}


void ticker_bredr_inquiry_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
			     uint32_t remainder, uint16_t lazy, uint8_t force,
			     void *param)
{
	static memq_link_t link;
	static struct mayfly mfy = {0, 0, &link, NULL, lll_bredr_inquiry_prepare};
	static struct lll_prepare_param p;
	struct ull_bredr_inquiry *inquiry = param;
	uint32_t ret;
	uint8_t ref;

	ARG_UNUSED(ticks_drift);

	/* Increment prepare reference count */
	ref = ull_ref_inc(&inquiry->ull);
	LL_ASSERT(ref);

	/* Append timing parameters */
	p.ticks_at_expire = ticks_at_expire;
	p.remainder = remainder;
	p.lazy = lazy;
	p.force = force;
	p.param = &inquiry->lll;
	mfy.param = &p;

	/* Kick LLL prepare via mayfly */
	ret = mayfly_enqueue(TICKER_USER_ID_ULL_HIGH,
			     TICKER_USER_ID_LLL, 0, &mfy);
	LL_ASSERT(!ret);
}

/*
 * Inquiry Scan Ticker
 */
int ticker_bredr_inquiry_scan_start(void *param, uint16_t interval,
				    uint16_t window)
{
	struct ull_bredr_inquiry_scan *inq_scan = param;
	uint32_t ticks_interval;
	uint32_t ticks_slot;
	uint32_t scan_dur_us;
	uint32_t volatile ret_cb;
	uint8_t ret;

	ticks_interval = ticker_bredr_slots_to_ticks(interval);

	/* Use computed scan duration, but cap at requested window */
	scan_dur_us = MIN(bredr_slice_params.scan_evt_dur,
			  (uint32_t)window * TICKER_BREDR_SLOT_US);
	ticks_slot = ticker_bredr_us_to_ticks(scan_dur_us);

	ret_cb = TICKER_STATUS_BUSY;
	ret = ticker_start(TICKER_INSTANCE_ID_CTLR,
			   TICKER_USER_ID_THREAD,
			   TICKER_ID_BREDR_INQUIRY_SCAN,
			   ticker_ticks_now_get(),
			   ticks_interval,
			   ticks_interval,
			   TICKER_NULL_REMAINDER,
			   TICKER_NULL_LAZY,
			   ticks_slot,
			   ticker_bredr_inquiry_scan_cb,
			   inq_scan,
			   ticker_op_start_cb,
			   (void *)&ret_cb);

	if (ret == TICKER_STATUS_BUSY) {
		while (ret_cb == TICKER_STATUS_BUSY) {
			k_yield();
		}
		ret = ret_cb;
	}

	if (ret != TICKER_STATUS_SUCCESS) {
		LOG_ERR("Inquiry scan ticker start failed: %u", ret);
		return -EIO;
	}

	LOG_DBG("Inquiry scan ticker started: interval=%u, window=%u",
		interval, window);
	return 0;
}

int ticker_bredr_inquiry_scan_stop(void)
{
	uint8_t ret;

	ret = ticker_stop(TICKER_INSTANCE_ID_CTLR,
			  TICKER_USER_ID_THREAD,
			  TICKER_ID_BREDR_INQUIRY_SCAN,
			  ticker_op_cb,
			  NULL);

	if (ret != TICKER_STATUS_SUCCESS && ret != TICKER_STATUS_BUSY) {
		return -EIO;
	}

	LOG_DBG("Inquiry scan ticker stopped");
	return 0;
}

void ticker_bredr_inquiry_scan_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
				  uint32_t remainder, uint16_t lazy, uint8_t force,
				  void *param)
{
	static memq_link_t link;
	static struct mayfly mfy = {0, 0, &link, NULL, lll_bredr_inquiry_scan_prepare};
	static struct lll_prepare_param p;
	struct ull_bredr_inquiry_scan *inq_scan = param;
	uint32_t ret;
	uint8_t ref;

	ARG_UNUSED(ticks_drift);

	/* Increment prepare reference count */
	ref = ull_ref_inc(&inq_scan->ull);
	LL_ASSERT(ref);

	/* Append timing parameters */
	p.ticks_at_expire = ticks_at_expire;
	p.remainder = remainder;
	p.lazy = lazy;
	p.force = force;
	p.param = &inq_scan->lll;
	mfy.param = &p;

	/* Kick LLL prepare via mayfly */
	ret = mayfly_enqueue(TICKER_USER_ID_ULL_HIGH,
			     TICKER_USER_ID_LLL, 0, &mfy);
	LL_ASSERT(!ret);
}

/*
 * Page Ticker
 */
int ticker_bredr_page_start(void *param, uint32_t ticks_anchor,
			    uint32_t ticks_slot)
{
	struct ull_bredr_page *page = param;
	uint32_t ticks_interval;
	uint32_t volatile ret_cb;
	uint8_t ret;

	/* Use computed scan duration */
	ticks_slot = ticker_bredr_us_to_ticks(bredr_slice_params.scan_evt_dur);
	ticks_interval = ticker_bredr_us_to_ticks(TICKER_BREDR_PAGE_INTERVAL_US);

	ret_cb = TICKER_STATUS_BUSY;
	ret = ticker_start(TICKER_INSTANCE_ID_CTLR,
			   TICKER_USER_ID_THREAD,
			   TICKER_ID_BREDR_PAGE,
			   ticks_anchor,
			   ticks_interval,
			   ticks_interval,
			   TICKER_NULL_REMAINDER,
			   TICKER_NULL_LAZY,
			   ticks_slot,
			   ticker_bredr_page_cb,
			   page,
			   ticker_op_start_cb,
			   (void *)&ret_cb);

	if (ret == TICKER_STATUS_BUSY) {
		while (ret_cb == TICKER_STATUS_BUSY) {
			k_yield();
		}
		ret = ret_cb;
	}

	if (ret != TICKER_STATUS_SUCCESS) {
		LOG_ERR("Page ticker start failed: %u", ret);
		return -EIO;
	}

	LOG_DBG("Page ticker started");
	return 0;
}

int ticker_bredr_page_stop(void)
{
	uint8_t ret;

	ret = ticker_stop(TICKER_INSTANCE_ID_CTLR,
			  TICKER_USER_ID_THREAD,
			  TICKER_ID_BREDR_PAGE,
			  ticker_op_cb,
			  NULL);

	if (ret != TICKER_STATUS_SUCCESS && ret != TICKER_STATUS_BUSY) {
		return -EIO;
	}

	LOG_DBG("Page ticker stopped");
	return 0;
}

void ticker_bredr_page_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
			  uint32_t remainder, uint16_t lazy, uint8_t force,
			  void *param)
{
	static memq_link_t link;
	static struct mayfly mfy = {0, 0, &link, NULL, lll_bredr_page_prepare};
	static struct lll_prepare_param p;
	struct ull_bredr_page *page = param;
	uint32_t ret;
	uint8_t ref;

	ARG_UNUSED(ticks_drift);

	/* Increment prepare reference count */
	ref = ull_ref_inc(&page->ull);
	LL_ASSERT(ref);

	/* Append timing parameters */
	p.ticks_at_expire = ticks_at_expire;
	p.remainder = remainder;
	p.lazy = lazy;
	p.force = force;
	p.param = &page->lll;
	mfy.param = &p;

	/* Kick LLL prepare via mayfly */
	ret = mayfly_enqueue(TICKER_USER_ID_ULL_HIGH,
			     TICKER_USER_ID_LLL, 0, &mfy);
	LL_ASSERT(!ret);
}

/*
 * Page Scan Ticker
 */
int ticker_bredr_page_scan_start(void *param, uint16_t interval,
				 uint16_t window)
{
	struct ull_bredr_page_scan *page_scan = param;
	uint32_t ticks_interval;
	uint32_t ticks_slot;
	uint32_t volatile ret_cb;
	uint8_t ret;

	ticks_interval = ticker_bredr_slots_to_ticks(interval);
	ticks_slot = ticker_bredr_slots_to_ticks(window);

	ret_cb = TICKER_STATUS_BUSY;
	ret = ticker_start(TICKER_INSTANCE_ID_CTLR,
			   TICKER_USER_ID_THREAD,
			   TICKER_ID_BREDR_PAGE_SCAN,
			   ticker_ticks_now_get(),
			   ticks_interval,
			   ticks_interval,
			   TICKER_NULL_REMAINDER,
			   TICKER_NULL_LAZY,
			   ticks_slot,
			   ticker_bredr_page_scan_cb,
			   page_scan,
			   ticker_op_start_cb,
			   (void *)&ret_cb);

	if (ret == TICKER_STATUS_BUSY) {
		while (ret_cb == TICKER_STATUS_BUSY) {
			k_yield();
		}
		ret = ret_cb;
	}

	if (ret != TICKER_STATUS_SUCCESS) {
		LOG_ERR("Page scan ticker start failed: %u", ret);
		return -EIO;
	}

	LOG_DBG("Page scan ticker started: interval=%u, window=%u",
		interval, window);
	return 0;
}

int ticker_bredr_page_scan_stop(void)
{
	uint8_t ret;

	ret = ticker_stop(TICKER_INSTANCE_ID_CTLR,
			  TICKER_USER_ID_THREAD,
			  TICKER_ID_BREDR_PAGE_SCAN,
			  ticker_op_cb,
			  NULL);

	if (ret != TICKER_STATUS_SUCCESS && ret != TICKER_STATUS_BUSY) {
		return -EIO;
	}

	LOG_DBG("Page scan ticker stopped");
	return 0;
}

void ticker_bredr_page_scan_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
			       uint32_t remainder, uint16_t lazy, uint8_t force,
			       void *param)
{
	static memq_link_t link;
	static struct mayfly mfy = {0, 0, &link, NULL, lll_bredr_page_scan_prepare};
	static struct lll_prepare_param p;
	struct ull_bredr_page_scan *page_scan = param;
	uint32_t ret;
	uint8_t ref;

	ARG_UNUSED(ticks_drift);

	/* Increment prepare reference count */
	ref = ull_ref_inc(&page_scan->ull);
	LL_ASSERT(ref);

	/* Append timing parameters */
	p.ticks_at_expire = ticks_at_expire;
	p.remainder = remainder;
	p.lazy = lazy;
	p.force = force;
	p.param = &page_scan->lll;
	mfy.param = &p;

	/* Kick LLL prepare via mayfly */
	ret = mayfly_enqueue(TICKER_USER_ID_ULL_HIGH,
			     TICKER_USER_ID_LLL, 0, &mfy);
	LL_ASSERT(!ret);
}


/*
 * ACL Connection Ticker
 */
int ticker_bredr_conn_start(uint8_t conn_idx, void *param,
			    uint32_t ticks_anchor, uint32_t ticks_slot,
			    uint16_t poll_interval)
{
	struct ull_bredr_conn *conn = param;
	uint32_t ticks_interval;
	uint32_t volatile ret_cb;
	uint8_t ticker_id;
	uint8_t ret;

	if (conn_idx >= ULL_BREDR_MAX_CONN) {
		return -EINVAL;
	}

	ticker_id = TICKER_ID_BREDR_CONN_BASE + conn_idx;
	ticks_interval = ticker_bredr_slots_to_ticks(poll_interval);

	ret_cb = TICKER_STATUS_BUSY;
	ret = ticker_start(TICKER_INSTANCE_ID_CTLR,
			   TICKER_USER_ID_THREAD,
			   ticker_id,
			   ticks_anchor,
			   ticks_interval,
			   ticks_interval,
			   TICKER_NULL_REMAINDER,
			   TICKER_NULL_LAZY,
			   ticks_slot,
			   ticker_bredr_conn_cb,
			   conn,
			   ticker_op_start_cb,
			   (void *)&ret_cb);

	if (ret == TICKER_STATUS_BUSY) {
		while (ret_cb == TICKER_STATUS_BUSY) {
			k_yield();
		}
		ret = ret_cb;
	}

	if (ret != TICKER_STATUS_SUCCESS) {
		LOG_ERR("Connection ticker start failed: %u", ret);
		return -EIO;
	}

	LOG_DBG("Connection ticker started: idx=%u, poll=%u", conn_idx, poll_interval);
	return 0;
}

int ticker_bredr_conn_stop(uint8_t conn_idx)
{
	uint8_t ticker_id;
	uint8_t ret;

	if (conn_idx >= ULL_BREDR_MAX_CONN) {
		return -EINVAL;
	}

	ticker_id = TICKER_ID_BREDR_CONN_BASE + conn_idx;

	ret = ticker_stop(TICKER_INSTANCE_ID_CTLR,
			  TICKER_USER_ID_THREAD,
			  ticker_id,
			  ticker_op_cb,
			  NULL);

	if (ret != TICKER_STATUS_SUCCESS && ret != TICKER_STATUS_BUSY) {
		return -EIO;
	}

	LOG_DBG("Connection ticker stopped: idx=%u", conn_idx);
	return 0;
}

int ticker_bredr_conn_update(uint8_t conn_idx, uint32_t ticks_drift_plus,
			     uint32_t ticks_drift_minus)
{
	uint8_t ticker_id;
	uint8_t ret;

	if (conn_idx >= ULL_BREDR_MAX_CONN) {
		return -EINVAL;
	}

	ticker_id = TICKER_ID_BREDR_CONN_BASE + conn_idx;

	ret = ticker_update(TICKER_INSTANCE_ID_CTLR,
			    TICKER_USER_ID_ULL_HIGH,
			    ticker_id,
			    ticks_drift_plus,
			    ticks_drift_minus,
			    0, 0,
			    TICKER_NULL_LAZY,
			    0,
			    ticker_op_cb,
			    NULL);

	if (ret != TICKER_STATUS_SUCCESS && ret != TICKER_STATUS_BUSY) {
		return -EIO;
	}

	return 0;
}

void ticker_bredr_conn_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
			  uint32_t remainder, uint16_t lazy, uint8_t force,
			  void *param)
{
	static memq_link_t link;
	static struct mayfly mfy = {0, 0, &link, NULL, lll_bredr_conn_prepare};
	static struct lll_prepare_param p;
	struct ull_bredr_conn *conn = param;
	uint32_t ret;
	uint8_t ref;

	ARG_UNUSED(ticks_drift);

	/* Check if connection is still valid */
	if (conn->lll.handle == 0xFFFF) {
		return;
	}

	/* Increment prepare reference count */
	ref = ull_ref_inc(&conn->ull);
	LL_ASSERT(ref);

	/* Append timing parameters */
	p.ticks_at_expire = ticks_at_expire;
	p.remainder = remainder;
	p.lazy = lazy;
	p.force = force;
	p.param = &conn->lll;
	mfy.param = &p;

	/* Kick LLL prepare via mayfly */
	ret = mayfly_enqueue(TICKER_USER_ID_ULL_HIGH,
			     TICKER_USER_ID_LLL, 0, &mfy);
	LL_ASSERT(!ret);

	/* Handle supervision timeout */
	if (lazy) {
		conn->supervision_expire += lazy;
	}
}

/*
 * SCO Ticker
 */
int ticker_bredr_sco_start(uint8_t sco_idx, void *param,
			   uint32_t ticks_anchor, uint8_t t_sco)
{
	struct ull_bredr_sco *sco = param;
	uint32_t ticks_interval;
	uint32_t ticks_slot;
	uint32_t volatile ret_cb;
	uint8_t ticker_id;
	uint8_t ret;

	if (sco_idx >= ULL_BREDR_SCO_MAX) {
		return -EINVAL;
	}

	ticker_id = TICKER_ID_BREDR_SCO_BASE + sco_idx;
	ticks_interval = ticker_bredr_slots_to_ticks(t_sco);
	ticks_slot = ticker_bredr_slots_to_ticks(1);  /* SCO uses 1 slot */

	ret_cb = TICKER_STATUS_BUSY;
	ret = ticker_start(TICKER_INSTANCE_ID_CTLR,
			   TICKER_USER_ID_THREAD,
			   ticker_id,
			   ticks_anchor,
			   ticks_interval,
			   ticks_interval,
			   TICKER_NULL_REMAINDER,
			   TICKER_LAZY_MUST_EXPIRE,  /* SCO must not miss */
			   ticks_slot,
			   ticker_bredr_sco_cb,
			   sco,
			   ticker_op_start_cb,
			   (void *)&ret_cb);

	if (ret == TICKER_STATUS_BUSY) {
		while (ret_cb == TICKER_STATUS_BUSY) {
			k_yield();
		}
		ret = ret_cb;
	}

	if (ret != TICKER_STATUS_SUCCESS) {
		LOG_ERR("SCO ticker start failed: %u", ret);
		return -EIO;
	}

	/* Update coexistence parameters */
	bredr_slice_params.sco_count++;
	if (t_sco < bredr_slice_params.sco_intv_min) {
		bredr_slice_params.sco_intv_min = t_sco;
	}
	ticker_bredr_slice_compute();

	LOG_DBG("SCO ticker started: idx=%u, t_sco=%u", sco_idx, t_sco);
	return 0;
}

int ticker_bredr_sco_stop(uint8_t sco_idx)
{
	uint8_t ticker_id;
	uint8_t ret;

	if (sco_idx >= ULL_BREDR_SCO_MAX) {
		return -EINVAL;
	}

	ticker_id = TICKER_ID_BREDR_SCO_BASE + sco_idx;

	ret = ticker_stop(TICKER_INSTANCE_ID_CTLR,
			  TICKER_USER_ID_THREAD,
			  ticker_id,
			  ticker_op_cb,
			  NULL);

	if (ret != TICKER_STATUS_SUCCESS && ret != TICKER_STATUS_BUSY) {
		return -EIO;
	}

	/* Update coexistence parameters */
	if (bredr_slice_params.sco_count > 0) {
		bredr_slice_params.sco_count--;
		if (bredr_slice_params.sco_count == 0) {
			bredr_slice_params.sco_intv_min = 0xFF;
		}
		ticker_bredr_slice_compute();
	}

	LOG_DBG("SCO ticker stopped: idx=%u", sco_idx);
	return 0;
}

void ticker_bredr_sco_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
			 uint32_t remainder, uint16_t lazy, uint8_t force,
			 void *param)
{
	static memq_link_t link;
	static struct mayfly mfy = {0, 0, &link, NULL, lll_bredr_sco_prepare};
	static struct lll_prepare_param p;
	struct ull_bredr_sco *sco = param;
	uint32_t ret;
	uint8_t ref;

	ARG_UNUSED(ticks_drift);

	/* SCO is time-critical, must always execute */
	if (lazy != TICKER_LAZY_MUST_EXPIRE) {
		/* Increment prepare reference count */
		ref = ull_ref_inc(&sco->ull);
		LL_ASSERT(ref);

		/* Append timing parameters */
		p.ticks_at_expire = ticks_at_expire;
		p.remainder = remainder;
		p.lazy = lazy;
		p.force = force;
		p.param = &sco->lll;
		mfy.param = &p;

		/* Kick LLL prepare via mayfly */
		ret = mayfly_enqueue(TICKER_USER_ID_ULL_HIGH,
				     TICKER_USER_ID_LLL, 0, &mfy);
		LL_ASSERT(!ret);
	}
}

/*
 * eSCO Ticker
 */
int ticker_bredr_esco_start(uint8_t esco_idx, void *param,
			    uint32_t ticks_anchor, uint8_t t_esco)
{
	struct ull_bredr_esco *esco = param;
	uint32_t ticks_interval;
	uint32_t ticks_slot;
	uint32_t volatile ret_cb;
	uint8_t ticker_id;
	uint8_t ret;

	if (esco_idx >= ULL_BREDR_ESCO_MAX) {
		return -EINVAL;
	}

	ticker_id = TICKER_ID_BREDR_ESCO_BASE + esco_idx;
	ticks_interval = ticker_bredr_slots_to_ticks(t_esco);
	ticks_slot = ticker_bredr_slots_to_ticks(2);  /* eSCO may use 2 slots */

	ret_cb = TICKER_STATUS_BUSY;
	ret = ticker_start(TICKER_INSTANCE_ID_CTLR,
			   TICKER_USER_ID_THREAD,
			   ticker_id,
			   ticks_anchor,
			   ticks_interval,
			   ticks_interval,
			   TICKER_NULL_REMAINDER,
			   TICKER_LAZY_MUST_EXPIRE,  /* eSCO must not miss */
			   ticks_slot,
			   ticker_bredr_esco_cb,
			   esco,
			   ticker_op_start_cb,
			   (void *)&ret_cb);

	if (ret == TICKER_STATUS_BUSY) {
		while (ret_cb == TICKER_STATUS_BUSY) {
			k_yield();
		}
		ret = ret_cb;
	}

	if (ret != TICKER_STATUS_SUCCESS) {
		LOG_ERR("eSCO ticker start failed: %u", ret);
		return -EIO;
	}

	/* Update coexistence parameters */
	bredr_slice_params.sco_count++;
	if (t_esco < bredr_slice_params.sco_intv_min) {
		bredr_slice_params.sco_intv_min = t_esco;
	}
	ticker_bredr_slice_compute();

	LOG_DBG("eSCO ticker started: idx=%u, t_esco=%u", esco_idx, t_esco);
	return 0;
}

int ticker_bredr_esco_stop(uint8_t esco_idx)
{
	uint8_t ticker_id;
	uint8_t ret;

	if (esco_idx >= ULL_BREDR_ESCO_MAX) {
		return -EINVAL;
	}

	ticker_id = TICKER_ID_BREDR_ESCO_BASE + esco_idx;

	ret = ticker_stop(TICKER_INSTANCE_ID_CTLR,
			  TICKER_USER_ID_THREAD,
			  ticker_id,
			  ticker_op_cb,
			  NULL);

	if (ret != TICKER_STATUS_SUCCESS && ret != TICKER_STATUS_BUSY) {
		return -EIO;
	}

	/* Update coexistence parameters */
	if (bredr_slice_params.sco_count > 0) {
		bredr_slice_params.sco_count--;
		if (bredr_slice_params.sco_count == 0) {
			bredr_slice_params.sco_intv_min = 0xFF;
		}
		ticker_bredr_slice_compute();
	}

	LOG_DBG("eSCO ticker stopped: idx=%u", esco_idx);
	return 0;
}

void ticker_bredr_esco_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
			  uint32_t remainder, uint16_t lazy, uint8_t force,
			  void *param)
{
	static memq_link_t link;
	static struct mayfly mfy = {0, 0, &link, NULL, lll_bredr_esco_prepare};
	static struct lll_prepare_param p;
	struct ull_bredr_esco *esco = param;
	uint32_t ret;
	uint8_t ref;

	ARG_UNUSED(ticks_drift);

	/* eSCO is time-critical, must always execute */
	if (lazy != TICKER_LAZY_MUST_EXPIRE) {
		/* Increment prepare reference count */
		ref = ull_ref_inc(&esco->ull);
		LL_ASSERT(ref);

		/* Append timing parameters */
		p.ticks_at_expire = ticks_at_expire;
		p.remainder = remainder;
		p.lazy = lazy;
		p.force = force;
		p.param = &esco->lll;
		mfy.param = &p;

		/* Kick LLL prepare via mayfly */
		ret = mayfly_enqueue(TICKER_USER_ID_ULL_HIGH,
				     TICKER_USER_ID_LLL, 0, &mfy);
		LL_ASSERT(!ret);
	}
}
