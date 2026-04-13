/* btp_avrcp.c - Bluetooth AVRCP Tester */

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
#define LOG_MODULE_NAME bttester_avrcp
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BTTESTER_LOG_LEVEL);

#include "btp/btp.h"
#include "z_api_port.h"

/* Forward declarations for z_api AVRCP functions */
extern int z_bt_avrcp_connect(const uint8_t *addr);
extern int z_bt_avrcp_disconnect(const uint8_t *addr);
extern int z_bt_avrcp_passthrough(const uint8_t *addr,
				  uint8_t key_id, uint8_t key_state);
extern int z_bt_avrcp_get_element_attrs(const uint8_t *addr);

static uint8_t supported_commands(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	struct btp_avrcp_read_supported_commands_rp *rp = rsp;

	tester_set_bit(rp->data, BTP_AVRCP_READ_SUPPORTED_COMMANDS);
	tester_set_bit(rp->data, BTP_AVRCP_CONNECT);
	tester_set_bit(rp->data, BTP_AVRCP_DISCONNECT);
	tester_set_bit(rp->data, BTP_AVRCP_SEND_PASSTHROUGH);
	tester_set_bit(rp->data, BTP_AVRCP_GET_ELEMENT_ATTRS);
	tester_set_bit(rp->data, BTP_AVRCP_REGISTER_NOTIFICATION);

	*rsp_len = sizeof(*rp) + 1;

	return BTP_STATUS_SUCCESS;
}

static uint8_t avrcp_connect(const void *cmd, uint16_t cmd_len,
			     void *rsp, uint16_t *rsp_len)
{
	const struct btp_avrcp_connect_cmd *cp = cmd;
	int err;

	LOG_INF("AVRCP connect");

	err = z_bt_avrcp_connect((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("AVRCP connect failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t avrcp_disconnect(const void *cmd, uint16_t cmd_len,
				void *rsp, uint16_t *rsp_len)
{
	const struct btp_avrcp_disconnect_cmd *cp = cmd;
	int err;

	LOG_INF("AVRCP disconnect");

	err = z_bt_avrcp_disconnect((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("AVRCP disconnect failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t avrcp_send_passthrough(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	const struct btp_avrcp_send_passthrough_cmd *cp = cmd;
	int err;

	LOG_INF("AVRCP passthrough: key_id=%d state=%d",
		cp->key_id, cp->key_state);

	err = z_bt_avrcp_passthrough((const uint8_t *)&cp->address,
				     cp->key_id, cp->key_state);
	if (err) {
		LOG_ERR("AVRCP passthrough failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t avrcp_get_element_attrs(const void *cmd, uint16_t cmd_len,
				       void *rsp, uint16_t *rsp_len)
{
	const struct btp_avrcp_get_element_attrs_cmd *cp = cmd;
	int err;

	LOG_INF("AVRCP get element attributes");

	err = z_bt_avrcp_get_element_attrs((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("AVRCP get element attrs failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t avrcp_register_notification(const void *cmd, uint16_t cmd_len,
					   void *rsp, uint16_t *rsp_len)
{
	const struct btp_avrcp_register_notification_cmd *cp = cmd;

	LOG_INF("AVRCP register notification: event_id=%d", cp->event_id);

	/* Notification registration is handled internally by the Framework.
	 * Return success to acknowledge the command.
	 */
	return BTP_STATUS_SUCCESS;
}

/* Callbacks from z_api AVRCP layer */
void btp_avrcp_connected_cb(const uint8_t *addr)
{
	struct btp_avrcp_connected_ev ev;

	LOG_INF("AVRCP connected");

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));

	tester_event(BTP_SERVICE_ID_AVRCP, BTP_AVRCP_EV_CONNECTED,
		     &ev, sizeof(ev));
}

void btp_avrcp_disconnected_cb(const uint8_t *addr)
{
	struct btp_avrcp_disconnected_ev ev;

	LOG_INF("AVRCP disconnected");

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));

	tester_event(BTP_SERVICE_ID_AVRCP, BTP_AVRCP_EV_DISCONNECTED,
		     &ev, sizeof(ev));
}

static const struct btp_handler handlers[] = {
	{
		.opcode = BTP_AVRCP_READ_SUPPORTED_COMMANDS,
		.index = BTP_INDEX_NONE,
		.expect_len = 0,
		.func = supported_commands,
	},
	{
		.opcode = BTP_AVRCP_CONNECT,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_avrcp_connect_cmd),
		.func = avrcp_connect,
	},
	{
		.opcode = BTP_AVRCP_DISCONNECT,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_avrcp_disconnect_cmd),
		.func = avrcp_disconnect,
	},
	{
		.opcode = BTP_AVRCP_SEND_PASSTHROUGH,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_avrcp_send_passthrough_cmd),
		.func = avrcp_send_passthrough,
	},
	{
		.opcode = BTP_AVRCP_GET_ELEMENT_ATTRS,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_avrcp_get_element_attrs_cmd),
		.func = avrcp_get_element_attrs,
	},
	{
		.opcode = BTP_AVRCP_REGISTER_NOTIFICATION,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_avrcp_register_notification_cmd),
		.func = avrcp_register_notification,
	},
};

uint8_t tester_init_avrcp(void)
{
	tester_register_command_handlers(BTP_SERVICE_ID_AVRCP, handlers,
					ARRAY_SIZE(handlers));

	LOG_INF("AVRCP service initialized");

	return BTP_STATUS_SUCCESS;
}

uint8_t tester_unregister_avrcp(void)
{
	LOG_INF("AVRCP service unregistered");

	return BTP_STATUS_SUCCESS;
}
