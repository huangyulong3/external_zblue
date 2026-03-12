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
 * BR/EDR Ticker Integration
 * Manages BR/EDR radio events using the shared ticker infrastructure
 * for BLE/BR/EDR coexistence
 */

#ifndef SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_TICKER_BREDR_H_
#define SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_TICKER_BREDR_H_

#include <zephyr/types.h>

/* Include lll.h only if not already included */
#ifndef SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_LLL_H_
#include "../lll.h"
#endif

/*
 * BR/EDR Ticker Timing Constants
 */
#define TICKER_BREDR_SLOT_US           625    /* BR/EDR slot duration */
#define TICKER_BREDR_HALF_SLOT_US      312    /* Half slot */

/* Inquiry timing */
#define TICKER_BREDR_INQUIRY_WINDOW_US (11250)  /* 18 slots */
#define TICKER_BREDR_INQUIRY_INTERVAL_US (11250)

/* Page timing */
#define TICKER_BREDR_PAGE_WINDOW_US    (11250)  /* 18 slots */
#define TICKER_BREDR_PAGE_INTERVAL_US  (11250)

/* Default scan intervals (in slots) */
#define TICKER_BREDR_SCAN_INTERVAL_DEFAULT  0x0800  /* 1.28s */
#define TICKER_BREDR_SCAN_WINDOW_DEFAULT    0x0012  /* 11.25ms */

/* ACL connection timing */
#define TICKER_BREDR_CONN_INTERVAL_US  (1250)   /* 2 slots minimum */
#define TICKER_BREDR_CONN_SLOT_US      (625)    /* 1 slot */

/* SCO timing */
#define TICKER_BREDR_SCO_INTERVAL_US   (3750)   /* 6 slots (HV3) */
#define TICKER_BREDR_SCO_SLOT_US       (625)

/*
 * Ticker Priority
 * BR/EDR priorities relative to BLE (used with ticker_priority_set)
 */
#define TICKER_BREDR_PRIORITY_SCO      (-64)   /* Highest - SCO is time-critical */
#define TICKER_BREDR_PRIORITY_ESCO     (-48)   /* High - eSCO is time-critical */
#define TICKER_BREDR_PRIORITY_CONN     (-32)   /* Medium-high - ACL connections */
#define TICKER_BREDR_PRIORITY_PAGE     (-16)   /* Medium - Paging */
#define TICKER_BREDR_PRIORITY_INQUIRY  (0)     /* Normal - Inquiry */
#define TICKER_BREDR_PRIORITY_SCAN     (16)    /* Low - Scanning */

/*
 * Function Declarations
 */

/* Initialization */
int ticker_bredr_init(void);
int ticker_bredr_reset(void);

/* Coexistence helper functions */
uint32_t ticker_bredr_scan_evt_dur_get(void);
uint32_t ticker_bredr_acl_evt_dur_get(void);

/* Inquiry ticker */
int ticker_bredr_inquiry_start(void *param, uint32_t ticks_anchor,
			       uint32_t ticks_slot);
int ticker_bredr_inquiry_stop(void);
void ticker_bredr_inquiry_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
			     uint32_t remainder, uint16_t lazy, uint8_t force,
			     void *param);

/* Inquiry scan ticker */
int ticker_bredr_inquiry_scan_start(void *param, uint16_t interval,
				    uint16_t window);
int ticker_bredr_inquiry_scan_stop(void);
void ticker_bredr_inquiry_scan_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
				  uint32_t remainder, uint16_t lazy, uint8_t force,
				  void *param);

/* Page ticker */
int ticker_bredr_page_start(void *param, uint32_t ticks_anchor,
			    uint32_t ticks_slot);
int ticker_bredr_page_stop(void);
void ticker_bredr_page_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
			  uint32_t remainder, uint16_t lazy, uint8_t force,
			  void *param);

/* Page scan ticker */
int ticker_bredr_page_scan_start(void *param, uint16_t interval,
				 uint16_t window);
int ticker_bredr_page_scan_stop(void);
void ticker_bredr_page_scan_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
			       uint32_t remainder, uint16_t lazy, uint8_t force,
			       void *param);

/* ACL connection ticker */
int ticker_bredr_conn_start(uint8_t conn_idx, void *param,
			    uint32_t ticks_anchor, uint32_t ticks_slot,
			    uint16_t poll_interval);
int ticker_bredr_conn_stop(uint8_t conn_idx);
int ticker_bredr_conn_update(uint8_t conn_idx, uint32_t ticks_drift_plus,
			     uint32_t ticks_drift_minus);
void ticker_bredr_conn_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
			  uint32_t remainder, uint16_t lazy, uint8_t force,
			  void *param);

/* SCO ticker */
int ticker_bredr_sco_start(uint8_t sco_idx, void *param,
			   uint32_t ticks_anchor, uint8_t t_sco);
int ticker_bredr_sco_stop(uint8_t sco_idx);
void ticker_bredr_sco_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
			 uint32_t remainder, uint16_t lazy, uint8_t force,
			 void *param);

/* eSCO ticker */
int ticker_bredr_esco_start(uint8_t esco_idx, void *param,
			    uint32_t ticks_anchor, uint8_t t_esco);
int ticker_bredr_esco_stop(uint8_t esco_idx);
void ticker_bredr_esco_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
			  uint32_t remainder, uint16_t lazy, uint8_t force,
			  void *param);

/* Utility functions */
uint32_t ticker_bredr_slots_to_ticks(uint16_t slots);
uint16_t ticker_bredr_ticks_to_slots(uint32_t ticks);
uint32_t ticker_bredr_us_to_ticks(uint32_t us);

#endif /* SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_TICKER_BREDR_H_ */
