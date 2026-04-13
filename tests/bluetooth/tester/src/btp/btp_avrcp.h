/* btp_avrcp.h - Bluetooth AVRCP tester headers */

/*
 * Copyright (c) 2026 XiaoMi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/addr.h>

/* AVRCP Service */
/* commands */
#define BTP_AVRCP_READ_SUPPORTED_COMMANDS	0x01
struct btp_avrcp_read_supported_commands_rp {
	uint8_t data[0];
} __packed;

#define BTP_AVRCP_CONNECT			0x02
struct btp_avrcp_connect_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_AVRCP_DISCONNECT			0x03
struct btp_avrcp_disconnect_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_AVRCP_SEND_PASSTHROUGH		0x04
struct btp_avrcp_send_passthrough_cmd {
	bt_addr_le_t address;
	uint8_t key_id;
	uint8_t key_state;
} __packed;

#define BTP_AVRCP_GET_ELEMENT_ATTRS		0x05
struct btp_avrcp_get_element_attrs_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_AVRCP_REGISTER_NOTIFICATION		0x06
struct btp_avrcp_register_notification_cmd {
	bt_addr_le_t address;
	uint8_t event_id;
} __packed;

/* events */
#define BTP_AVRCP_EV_CONNECTED			0x80
struct btp_avrcp_connected_ev {
	bt_addr_le_t address;
} __packed;

#define BTP_AVRCP_EV_DISCONNECTED		0x81
struct btp_avrcp_disconnected_ev {
	bt_addr_le_t address;
} __packed;

#define BTP_AVRCP_EV_NOTIFICATION		0x82
struct btp_avrcp_notification_ev {
	bt_addr_le_t address;
	uint8_t event_id;
	uint8_t data_len;
	uint8_t data[];
} __packed;
