/* btp_hid.h - Bluetooth HID tester headers */

/*
 * Copyright (c) 2026 XiaoMi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/addr.h>

/* HID Service */
/* commands */
#define BTP_HID_READ_SUPPORTED_COMMANDS		0x01
struct btp_hid_read_supported_commands_rp {
	uint8_t data[0];
} __packed;

#define BTP_HID_CONNECT				0x02
struct btp_hid_connect_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_HID_DISCONNECT			0x03
struct btp_hid_disconnect_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_HID_REGISTER_DEVICE			0x04
struct btp_hid_register_device_cmd {
	uint8_t sub_class;
} __packed;

#define BTP_HID_SEND_REPORT			0x05
struct btp_hid_send_report_cmd {
	bt_addr_le_t address;
	uint8_t report_id;
	uint16_t data_len;
	uint8_t data[];
} __packed;

/* events */
#define BTP_HID_EV_CONNECTED			0x80
struct btp_hid_connected_ev {
	bt_addr_le_t address;
} __packed;

#define BTP_HID_EV_DISCONNECTED			0x81
struct btp_hid_disconnected_ev {
	bt_addr_le_t address;
} __packed;
