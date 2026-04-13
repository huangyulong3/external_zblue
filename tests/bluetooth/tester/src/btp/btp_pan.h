/* btp_pan.h - Bluetooth PAN tester headers */

/*
 * Copyright (c) 2026 XiaoMi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/addr.h>

/* PAN Service */
/* commands */
#define BTP_PAN_READ_SUPPORTED_COMMANDS		0x01
struct btp_pan_read_supported_commands_rp {
	uint8_t data[0];
} __packed;

#define BTP_PAN_CONNECT				0x02
struct btp_pan_connect_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_PAN_DISCONNECT			0x03
struct btp_pan_disconnect_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_PAN_SET_ROLE			0x04
struct btp_pan_set_role_cmd {
	uint8_t role;
} __packed;

/* events */
#define BTP_PAN_EV_CONNECTED			0x80
struct btp_pan_connected_ev {
	bt_addr_le_t address;
} __packed;

#define BTP_PAN_EV_DISCONNECTED			0x81
struct btp_pan_disconnected_ev {
	bt_addr_le_t address;
} __packed;
