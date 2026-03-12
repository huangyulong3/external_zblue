/*
 * Copyright (c) 2024 Xiaomi Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * SWI (Software Interrupt) vendor HAL for NuttX QEMU (Goldfish ARM) port.
 * Uses GIC SGI (Software Generated Interrupts) for interrupt-context
 * execution of BLE Controller ISR handlers.
 *
 * GICv2 SGI 0-15 are always-enabled and cannot be individually
 * masked via up_enable_irq/up_disable_irq. Use up_irq_save/restore
 * for critical sections.
 */

#ifndef HAL_SWI_VENDOR_HAL_H_
#define HAL_SWI_VENDOR_HAL_H_

#include <stdint.h>

/* GIC SGI IRQ assignments for BLE Controller
 * SGI 0-3 reserved (SMP IPI on some configs), we use SGI 4-7.
 */

#define BT_CTLR_IRQ_RADIO      4   /* Radio ISR (isr_radio) */
#define BT_CTLR_IRQ_TICKER     5   /* Ticker ISR (rtc0 timer) */
#define BT_CTLR_IRQ_SWI_LLL    6   /* SWI LLL (swi_lll) */
#define BT_CTLR_IRQ_SWI_ULL    7   /* SWI ULL LOW (swi_ull_low) */

/* Legacy defines mapped to SGI numbers */
#define HAL_SWI_RADIO_IRQ       BT_CTLR_IRQ_SWI_LLL
#define HAL_SWI_WORKER_IRQ      BT_CTLR_IRQ_SWI_LLL
#define HAL_SWI_JOB_IRQ         BT_CTLR_IRQ_SWI_ULL

/* Virtual radio/RTC IRQ numbers mapped to SGI */
#define HAL_RADIO_IRQn          BT_CTLR_IRQ_RADIO
#define HAL_RTC_IRQn            BT_CTLR_IRQ_TICKER

/* SWI functions */
void hal_swi_init(void);
void hal_swi_lll_pend(void);
void hal_swi_ull_low_pend(void);
void hal_swi_radio_pend(void);

#endif /* HAL_SWI_VENDOR_HAL_H_ */
