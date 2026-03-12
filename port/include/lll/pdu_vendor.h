/*
 * Copyright (c) 2024 Xiaomi Corporation
 * SPDX-License-Identifier: Apache-2.0
 *
 * PDU vendor definitions stub for NuttX port
 */

#ifndef LLL_PDU_VENDOR_H_
#define LLL_PDU_VENDOR_H_

/* Note: pdu_df.h is included by pdu.h, do not include it here to avoid
 * redefinition of struct pdu_cte_info
 */

#if defined(CONFIG_BT_CTLR_DATA_LENGTH_CLEAR)
#define OCTET3_LEN 0U
#else
#define OCTET3_LEN 1U
#endif

/* Forward declaration - struct pdu_cte_info is defined in pdu_df.h */
struct pdu_cte_info;

/* Presence of vendor Data PDU struct octet3 */
struct pdu_data_vnd_octet3 {
    union {
        uint8_t resv[OCTET3_LEN];
#if !defined(CONFIG_BT_CTLR_DATA_LENGTH_CLEAR)
        /* Use pointer to avoid including pdu_df.h here */
        uint8_t cte_info_data[sizeof(uint8_t)]; /* placeholder for cte_info */
#endif
    } __packed;
} __packed;

/* Presence of vendor BIS PDU struct octet3 */
struct pdu_bis_vnd_octet3 {
    union {
        uint8_t resv[OCTET3_LEN];
    } __packed;
} __packed;

/* Presence of vendor CIS PDU struct octet3 */
struct pdu_cis_vnd_octet3 {
    union {
        uint8_t resv[OCTET3_LEN];
    } __packed;
} __packed;

/* Presence of ISOAL helper vendor ISO PDU struct octet3 */
struct pdu_iso_vnd_octet3 {
    union {
        uint8_t resv[OCTET3_LEN];
    } __packed;
} __packed;

#endif /* LLL_PDU_VENDOR_H_ */
