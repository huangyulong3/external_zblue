/* btp_rfcomm.h - Bluetooth RFCOMM tester headers */

/*
 * Copyright (c) 2026 XiaoMi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/addr.h>

/* RFCOMM Service */
/* commands */
#define BTP_RFCOMM_READ_SUPPORTED_COMMANDS	0x01
struct btp_rfcomm_read_supported_commands_rp {
	uint8_t data[0];
} __packed;

#define BTP_RFCOMM_CONNECT			0x02
struct btp_rfcomm_connect_cmd {
	bt_addr_le_t address;
	uint8_t channel;
} __packed;

#define BTP_RFCOMM_DISCONNECT			0x03
struct btp_rfcomm_disconnect_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_RFCOMM_LISTEN			0x05
struct btp_rfcomm_listen_cmd {
	uint8_t channel;
} __packed;

#define BTP_RFCOMM_SEND_DATA			0x04
struct btp_rfcomm_send_data_cmd {
	bt_addr_le_t address;
	uint16_t data_len;
	uint8_t data[];
} __packed;

/* events */
#define BTP_RFCOMM_EV_CONNECTED			0x80
struct btp_rfcomm_connected_ev {
	bt_addr_le_t address;
	uint8_t channel;
} __packed;

#define BTP_RFCOMM_EV_DISCONNECTED		0x81
struct btp_rfcomm_disconnected_ev {
	bt_addr_le_t address;
} __packed;

#define BTP_RFCOMM_EV_DATA_RECEIVED		0x82
struct btp_rfcomm_data_received_ev {
	bt_addr_le_t address;
	uint16_t data_len;
	uint8_t data[];
} __packed;
