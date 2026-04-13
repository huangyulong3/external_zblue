/* btp_a2dp.h - Bluetooth A2DP tester headers */

/*
 * Copyright (c) 2026 XiaoMi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/addr.h>

/* A2DP Service */
/* commands */
#define BTP_A2DP_READ_SUPPORTED_COMMANDS	0x01
struct btp_a2dp_read_supported_commands_rp {
	uint8_t data[0];
} __packed;

#define BTP_A2DP_CONNECT			0x02
struct btp_a2dp_connect_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_A2DP_DISCONNECT			0x03
struct btp_a2dp_disconnect_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_A2DP_START_STREAM			0x04
struct btp_a2dp_start_stream_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_A2DP_STOP_STREAM			0x05
struct btp_a2dp_stop_stream_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_A2DP_SET_CONFIGURATION		0x06
struct btp_a2dp_set_configuration_cmd {
	bt_addr_le_t address;
	uint8_t codec_id;
} __packed;

#define BTP_A2DP_GET_CONFIGURATION		0x07
struct btp_a2dp_get_configuration_cmd {
	bt_addr_le_t address;
} __packed;

/* events */
#define BTP_A2DP_EV_CONNECTED			0x80
struct btp_a2dp_connected_ev {
	bt_addr_le_t address;
} __packed;

#define BTP_A2DP_EV_DISCONNECTED		0x81
struct btp_a2dp_disconnected_ev {
	bt_addr_le_t address;
} __packed;

#define BTP_A2DP_EV_AUDIO_STATE			0x82
struct btp_a2dp_audio_state_ev {
	bt_addr_le_t address;
	uint8_t state;
} __packed;
