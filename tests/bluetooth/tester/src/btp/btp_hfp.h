/* btp_hfp.h - Bluetooth HFP tester headers */

/*
 * Copyright (c) 2026 XiaoMi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/addr.h>

/* HFP Service */
/* commands */
#define BTP_HFP_READ_SUPPORTED_COMMANDS		0x01
struct btp_hfp_read_supported_commands_rp {
	uint8_t data[0];
} __packed;

#define BTP_HFP_CONNECT				0x02
struct btp_hfp_connect_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_HFP_DISCONNECT			0x03
struct btp_hfp_disconnect_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_HFP_ANSWER_CALL			0x04
struct btp_hfp_answer_call_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_HFP_REJECT_CALL			0x05
struct btp_hfp_reject_call_cmd {
	bt_addr_le_t address;
} __packed;

#define BTP_HFP_DIAL				0x06
struct btp_hfp_dial_cmd {
	bt_addr_le_t address;
	uint8_t number_len;
	uint8_t number[];
} __packed;

#define BTP_HFP_SET_VOLUME			0x07
struct btp_hfp_set_volume_cmd {
	bt_addr_le_t address;
	uint8_t type;
	uint8_t volume;
} __packed;

#define BTP_HFP_SEND_DTMF			0x08
struct btp_hfp_send_dtmf_cmd {
	bt_addr_le_t address;
	uint8_t code;
} __packed;

/* events */
#define BTP_HFP_EV_CONNECTED			0x80
struct btp_hfp_connected_ev {
	bt_addr_le_t address;
} __packed;

#define BTP_HFP_EV_DISCONNECTED			0x81
struct btp_hfp_disconnected_ev {
	bt_addr_le_t address;
} __packed;

#define BTP_HFP_EV_CALL_STATE			0x82
struct btp_hfp_call_state_ev {
	bt_addr_le_t address;
	uint8_t state;
} __packed;

#define BTP_HFP_EV_AUDIO_STATE			0x83
struct btp_hfp_audio_state_ev {
	bt_addr_le_t address;
	uint8_t state;
} __packed;
