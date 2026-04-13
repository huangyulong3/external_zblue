/* btp_sdp.h - Bluetooth SDP tester headers */

/*
 * Copyright (c) 2026 XiaoMi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/addr.h>

/* SDP Service */
/* commands */
#define BTP_SDP_READ_SUPPORTED_COMMANDS		0x01
struct btp_sdp_read_supported_commands_rp {
	uint8_t data[0];
} __packed;

#define BTP_SDP_SEARCH_REQ			0x02
struct btp_sdp_search_req_cmd {
	bt_addr_le_t address;
	uint16_t uuid;
} __packed;

#define BTP_SDP_ATTR_REQ			0x03
struct btp_sdp_attr_req_cmd {
	bt_addr_le_t address;
	uint32_t service_record_handle;
} __packed;

#define BTP_SDP_SEARCH_ATTR_REQ			0x04
struct btp_sdp_search_attr_req_cmd {
	bt_addr_le_t address;
	uint16_t uuid;
} __packed;

/* events */
#define BTP_SDP_EV_SEARCH_RESULT		0x80
struct btp_sdp_search_result_ev {
	bt_addr_le_t address;
	uint32_t service_record_handle;
} __packed;

#define BTP_SDP_EV_ATTR_RESULT			0x81
struct btp_sdp_attr_result_ev {
	bt_addr_le_t address;
	uint16_t attr_data_len;
	uint8_t attr_data[];
} __packed;

#define BTP_SDP_EV_SEARCH_ATTR_RESULT		0x82
struct btp_sdp_search_attr_result_ev {
	bt_addr_le_t address;
	uint16_t attr_data_len;
	uint8_t attr_data[];
} __packed;
