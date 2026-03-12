/*
 * Copyright (c) 2024 Xiaomi Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * LLL vendor definitions stub for NuttX port
 */

#ifndef LLL_LLL_VENDOR_H_
#define LLL_LLL_VENDOR_H_

#include "hal/ticker_vendor_hal.h"

/* Event overhead timing definitions */
#define EVENT_OVERHEAD_XTAL_US        1500
#define EVENT_OVERHEAD_PREEMPT_US     0
#define EVENT_OVERHEAD_PREEMPT_MIN_US 0
#define EVENT_OVERHEAD_PREEMPT_MAX_US EVENT_OVERHEAD_XTAL_US
#define EVENT_OVERHEAD_START_US       275
#define EVENT_OVERHEAD_END_US         40
#define EVENT_JITTER_US               16
#define EVENT_TIES_US                 625

/* Ticker resolution margin */
#define EVENT_TICKER_RES_MARGIN_US    32

/* RX timing */
#define EVENT_RX_JITTER_US(phy)       16
#define EVENT_RX_TO_US(phy)           ((((((phy)&0x03) + 4)<<3)/BIT((((phy)&0x3)>>1))) + \
                                       EVENT_RX_JITTER_US(phy))

/* Turnaround time */
#define EVENT_RX_TX_TURNAROUND(phy)   150

/* Sub-microsecond conversion macros */
#define EVENT_US_TO_US_FRAC(us)             (us)
#define EVENT_US_FRAC_TO_US(us_frac)        (us_frac)
#define EVENT_TICKS_TO_US_FRAC(ticks)       HAL_TICKER_TICKS_TO_US(ticks)
#define EVENT_US_FRAC_TO_TICKS(us_frac)     HAL_TICKER_US_TO_TICKS(us_frac)
#define EVENT_US_FRAC_TO_REMAINDER(us_frac) HAL_TICKER_REMAINDER(us_frac)

/* CIS setup overhead */
#define EVENT_OVERHEAD_CIS_SETUP_US         500U

#endif /* LLL_LLL_VENDOR_H_ */
