/*
 * Copyright (c) 2024 Xiaomi Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPU vendor HAL stub for NuttX port
 */

#ifndef HAL_CPU_VENDOR_HAL_H_
#define HAL_CPU_VENDOR_HAL_H_

#include <stdint.h>

/* Memory barrier stubs */
static inline void cpu_dmb(void)
{
    __asm__ volatile ("" : : : "memory");
}

static inline void cpu_dsb(void)
{
    __asm__ volatile ("" : : : "memory");
}

static inline void cpu_isb(void)
{
    __asm__ volatile ("" : : : "memory");
}

/* Sleep/WFE stubs */
static inline void cpu_sleep(void) {}
static inline void cpu_wfe(void) {}

#endif /* HAL_CPU_VENDOR_HAL_H_ */
