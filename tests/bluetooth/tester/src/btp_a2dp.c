/* btp_a2dp.c - Bluetooth A2DP Tester */

/*
 * Copyright (c) 2026 XiaoMi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/atomic.h>
#include <zephyr/types.h>
#include <string.h>

#include <zephyr/toolchain.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
#define LOG_MODULE_NAME bttester_a2dp
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BTTESTER_LOG_LEVEL);

#include "btp/btp.h"
#include "z_api_port.h"

/* Forward declarations for z_api A2DP functions */
extern int z_bt_a2dp_connect(const uint8_t *addr);
extern int z_bt_a2dp_disconnect(const uint8_t *addr);
extern int z_bt_a2dp_start(const uint8_t *addr);
extern int z_bt_a2dp_stop(const uint8_t *addr);

static uint8_t supported_commands(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	struct btp_a2dp_read_supported_commands_rp *rp = rsp;

	tester_set_bit(rp->data, BTP_A2DP_READ_SUPPORTED_COMMANDS);
	tester_set_bit(rp->data, BTP_A2DP_CONNECT);
	tester_set_bit(rp->data, BTP_A2DP_DISCONNECT);
	tester_set_bit(rp->data, BTP_A2DP_START_STREAM);
	tester_set_bit(rp->data, BTP_A2DP_STOP_STREAM);
	tester_set_bit(rp->data, BTP_A2DP_SET_CONFIGURATION);
	tester_set_bit(rp->data, BTP_A2DP_GET_CONFIGURATION);

	*rsp_len = sizeof(*rp) + 1;

	return BTP_STATUS_SUCCESS;
}

static uint8_t a2dp_connect(const void *cmd, uint16_t cmd_len,
			    void *rsp, uint16_t *rsp_len)
{
	const struct btp_a2dp_connect_cmd *cp = cmd;
	int err;

	LOG_INF("A2DP connect");

	err = z_bt_a2dp_connect((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("A2DP connect failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t a2dp_disconnect(const void *cmd, uint16_t cmd_len,
			       void *rsp, uint16_t *rsp_len)
{
	const struct btp_a2dp_disconnect_cmd *cp = cmd;
	int err;

	LOG_INF("A2DP disconnect");

	err = z_bt_a2dp_disconnect((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("A2DP disconnect failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t a2dp_start_stream(const void *cmd, uint16_t cmd_len,
				 void *rsp, uint16_t *rsp_len)
{
	const struct btp_a2dp_start_stream_cmd *cp = cmd;
	int err;

	LOG_INF("A2DP start stream");

	err = z_bt_a2dp_start((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("A2DP start stream failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t a2dp_stop_stream(const void *cmd, uint16_t cmd_len,
				void *rsp, uint16_t *rsp_len)
{
	const struct btp_a2dp_stop_stream_cmd *cp = cmd;
	int err;

	LOG_INF("A2DP stop stream");

	err = z_bt_a2dp_stop((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("A2DP stop stream failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t a2dp_set_configuration(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	LOG_INF("A2DP set configuration");

	/* Configuration is handled internally by the Framework.
	 * Return success to acknowledge the command.
	 */
	return BTP_STATUS_SUCCESS;
}

static uint8_t a2dp_get_configuration(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	LOG_INF("A2DP get configuration");

	return BTP_STATUS_SUCCESS;
}

/* Callbacks from z_api A2DP layer */
void btp_a2dp_connected_cb(const uint8_t *addr)
{
	struct btp_a2dp_connected_ev ev;

	LOG_INF("A2DP connected");

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));

	tester_event(BTP_SERVICE_ID_A2DP, BTP_A2DP_EV_CONNECTED,
		     &ev, sizeof(ev));
}

void btp_a2dp_disconnected_cb(const uint8_t *addr)
{
	struct btp_a2dp_disconnected_ev ev;

	LOG_INF("A2DP disconnected");

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));

	tester_event(BTP_SERVICE_ID_A2DP, BTP_A2DP_EV_DISCONNECTED,
		     &ev, sizeof(ev));
}

void btp_a2dp_audio_state_cb(const uint8_t *addr, uint8_t state)
{
	struct btp_a2dp_audio_state_ev ev;

	LOG_INF("A2DP audio state: %d", state);

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));
	ev.state = state;

	tester_event(BTP_SERVICE_ID_A2DP, BTP_A2DP_EV_AUDIO_STATE,
		     &ev, sizeof(ev));
}

static const struct btp_handler handlers[] = {
	{
		.opcode = BTP_A2DP_READ_SUPPORTED_COMMANDS,
		.index = BTP_INDEX_NONE,
		.expect_len = 0,
		.func = supported_commands,
	},
	{
		.opcode = BTP_A2DP_CONNECT,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_a2dp_connect_cmd),
		.func = a2dp_connect,
	},
	{
		.opcode = BTP_A2DP_DISCONNECT,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_a2dp_disconnect_cmd),
		.func = a2dp_disconnect,
	},
	{
		.opcode = BTP_A2DP_START_STREAM,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_a2dp_start_stream_cmd),
		.func = a2dp_start_stream,
	},
	{
		.opcode = BTP_A2DP_STOP_STREAM,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_a2dp_stop_stream_cmd),
		.func = a2dp_stop_stream,
	},
	{
		.opcode = BTP_A2DP_SET_CONFIGURATION,
		.index = BTP_INDEX,
		.expect_len = -1,
		.func = a2dp_set_configuration,
	},
	{
		.opcode = BTP_A2DP_GET_CONFIGURATION,
		.index = BTP_INDEX,
		.expect_len = -1,
		.func = a2dp_get_configuration,
	},
};

uint8_t tester_init_a2dp(void)
{
	tester_register_command_handlers(BTP_SERVICE_ID_A2DP, handlers,
					ARRAY_SIZE(handlers));

	LOG_INF("A2DP service initialized");

	return BTP_STATUS_SUCCESS;
}

uint8_t tester_unregister_a2dp(void)
{
	LOG_INF("A2DP service unregistered");

	return BTP_STATUS_SUCCESS;
}
