/*
 * Copyright (c) 2024 Xiaomi Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * LLL advertising types stub for NuttX port
 */

#ifndef LLL_LLL_ADV_TYPES_H_
#define LLL_LLL_ADV_TYPES_H_

#include <stdint.h>

#ifndef DOUBLE_BUFFER_SIZE
#define DOUBLE_BUFFER_SIZE 2
#endif

/* Structure used to double buffer pointers of AD Data PDU buffer */
struct lll_adv_pdu {
    uint8_t volatile first;
    uint8_t          last;
    uint8_t          *pdu[DOUBLE_BUFFER_SIZE];
#if defined(CONFIG_BT_CTLR_ADV_EXT_PDU_EXTRA_DATA_MEMORY)
    void             *extra_data[DOUBLE_BUFFER_SIZE];
#endif
};

#endif /* LLL_LLL_ADV_TYPES_H_ */
