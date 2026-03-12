/*
 * Copyright (c) 2024 Xiaomi Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * Debug vendor HAL stub for NuttX port
 */

#ifndef HAL_DEBUG_VENDOR_HAL_H_
#define HAL_DEBUG_VENDOR_HAL_H_

/* Debug pin stubs - no-op for NuttX port */
#define DEBUG_INIT()
#define DEBUG_CPU_SLEEP(flag)
#define DEBUG_TICKER_ISR(flag)
#define DEBUG_TICKER_TASK(flag)
#define DEBUG_TICKER_JOB(flag)
#define DEBUG_RADIO_ISR(flag)
#define DEBUG_RADIO_XTAL(flag)
#define DEBUG_RADIO_ACTIVE(flag)
#define DEBUG_RADIO_CLOSE(flag)
#define DEBUG_RADIO_CLOSE_A(flag)
#define DEBUG_RADIO_CLOSE_S(flag)
#define DEBUG_RADIO_CLOSE_M(flag)
#define DEBUG_RADIO_CLOSE_O(flag)
#define DEBUG_RADIO_PREPARE_A(flag)
#define DEBUG_RADIO_START_A(flag)
#define DEBUG_RADIO_PREPARE_S(flag)
#define DEBUG_RADIO_START_S(flag)
#define DEBUG_RADIO_PREPARE_O(flag)
#define DEBUG_RADIO_START_O(flag)
#define DEBUG_RADIO_PREPARE_M(flag)
#define DEBUG_RADIO_START_M(flag)

#endif /* HAL_DEBUG_VENDOR_HAL_H_ */
