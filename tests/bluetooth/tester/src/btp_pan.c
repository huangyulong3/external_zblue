/* btp_pan.c - Bluetooth PAN Tester */

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
#define LOG_MODULE_NAME bttester_pan
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BTTESTER_LOG_LEVEL);

#include "btp/btp.h"
#include "z_api_port.h"

/* Forward declarations for z_api PAN functions */
extern int z_bt_pan_connect(const uint8_t *addr);
extern int z_bt_pan_disconnect(const uint8_t *addr);
extern int z_bt_pan_set_role(uint8_t role);

static uint8_t supported_commands(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	struct btp_pan_read_supported_commands_rp *rp = rsp;

	tester_set_bit(rp->data, BTP_PAN_READ_SUPPORTED_COMMANDS);
	tester_set_bit(rp->data, BTP_PAN_CONNECT);
	tester_set_bit(rp->data, BTP_PAN_DISCONNECT);
	tester_set_bit(rp->data, BTP_PAN_SET_ROLE);

	*rsp_len = sizeof(*rp) + 1;

	return BTP_STATUS_SUCCESS;
}

static uint8_t pan_connect(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_pan_connect_cmd *cp = cmd;
	int err;

	LOG_INF("PAN connect");

	err = z_bt_pan_connect((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("PAN connect failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t pan_disconnect(const void *cmd, uint16_t cmd_len,
			      void *rsp, uint16_t *rsp_len)
{
	const struct btp_pan_disconnect_cmd *cp = cmd;
	int err;

	LOG_INF("PAN disconnect");

	err = z_bt_pan_disconnect((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("PAN disconnect failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t pan_set_role(const void *cmd, uint16_t cmd_len,
			    void *rsp, uint16_t *rsp_len)
{
	const struct btp_pan_set_role_cmd *cp = cmd;
	int err;

	LOG_INF("PAN set role: %d", cp->role);

	err = z_bt_pan_set_role(cp->role);
	if (err) {
		LOG_ERR("PAN set role failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

/* Callbacks from z_api PAN layer */
void btp_pan_connected_cb(const uint8_t *addr)
{
	struct btp_pan_connected_ev ev;

	LOG_INF("PAN connected");

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));

	tester_event(BTP_SERVICE_ID_PAN, BTP_PAN_EV_CONNECTED,
		     &ev, sizeof(ev));
}

void btp_pan_disconnected_cb(const uint8_t *addr)
{
	struct btp_pan_disconnected_ev ev;

	LOG_INF("PAN disconnected");

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));

	tester_event(BTP_SERVICE_ID_PAN, BTP_PAN_EV_DISCONNECTED,
		     &ev, sizeof(ev));
}

static const struct btp_handler handlers[] = {
	{
		.opcode = BTP_PAN_READ_SUPPORTED_COMMANDS,
		.index = BTP_INDEX_NONE,
		.expect_len = 0,
		.func = supported_commands,
	},
	{
		.opcode = BTP_PAN_CONNECT,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_pan_connect_cmd),
		.func = pan_connect,
	},
	{
		.opcode = BTP_PAN_DISCONNECT,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_pan_disconnect_cmd),
		.func = pan_disconnect,
	},
	{
		.opcode = BTP_PAN_SET_ROLE,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_pan_set_role_cmd),
		.func = pan_set_role,
	},
};

uint8_t tester_init_pan(void)
{
	tester_register_command_handlers(BTP_SERVICE_ID_PAN, handlers,
					ARRAY_SIZE(handlers));

	LOG_INF("PAN service initialized");

	return BTP_STATUS_SUCCESS;
}

uint8_t tester_unregister_pan(void)
{
	LOG_INF("PAN service unregistered");

	return BTP_STATUS_SUCCESS;
}
