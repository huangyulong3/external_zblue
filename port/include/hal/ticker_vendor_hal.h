/*
 * Copyright (c) 2024 Xiaomi Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ticker vendor HAL stub for NuttX port
 */

#ifndef HAL_TICKER_VENDOR_HAL_H_
#define HAL_TICKER_VENDOR_HAL_H_

/* Ticker ID definitions for BR/EDR (these are not defined elsewhere) */
#define TICKER_ID_BREDR_INQUIRY         32
#define TICKER_ID_BREDR_INQUIRY_SCAN    33
#define TICKER_ID_BREDR_PAGE            34
#define TICKER_ID_BREDR_PAGE_SCAN       35
#define TICKER_ID_BREDR_CONN_BASE       36
#define TICKER_ID_BREDR_SCO_BASE        48
#define TICKER_ID_BREDR_ESCO_BASE       56

/* Stub definitions for ticker HAL */
#define HAL_TICKER_CNTR_CLK_FREQ_HZ     32768
#define HAL_TICKER_CNTR_CMP_OFFSET_MIN  3
#define HAL_TICKER_CNTR_SET_LATENCY     0
#define HAL_TICKER_CNTR_MSBIT           23
#define HAL_TICKER_CNTR_MASK            0x00FFFFFF
#define HAL_TICKER_RESCHEDULE_MARGIN    0
#define HAL_TICKER_REMAINDER_RANGE      HAL_TICKER_CNTR_CLK_FREQ_HZ
#define HAL_TICKER_CNTR_CLK_UNIT_FSEC   30517578125UL
#define HAL_TICKER_FSEC_PER_USEC        1000000000UL

#define HAL_TICKER_US_TO_TICKS(x)       (((x) * HAL_TICKER_CNTR_CLK_FREQ_HZ) / 1000000)
#define HAL_TICKER_TICKS_TO_US(x)       (((x) * 1000000) / HAL_TICKER_CNTR_CLK_FREQ_HZ)
#define HAL_TICKER_US_TO_TICKS_CEIL(x)  ((((x) * HAL_TICKER_CNTR_CLK_FREQ_HZ) + 999999) / 1000000)

#define HAL_TICKER_REMAINDER(x)         0

#endif /* HAL_TICKER_VENDOR_HAL_H_ */
