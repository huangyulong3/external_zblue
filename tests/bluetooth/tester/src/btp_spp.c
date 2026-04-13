/* btp_spp.c - Bluetooth SPP Tester */

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
#define LOG_MODULE_NAME bttester_spp
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BTTESTER_LOG_LEVEL);

#include "btp/btp.h"
#include "z_api_port.h"

static uint8_t supported_commands(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	struct btp_spp_read_supported_commands_rp *rp = rsp;

	tester_set_bit(rp->data, BTP_SPP_READ_SUPPORTED_COMMANDS);
	tester_set_bit(rp->data, BTP_SPP_CONNECT);
	tester_set_bit(rp->data, BTP_SPP_DISCONNECT);
	tester_set_bit(rp->data, BTP_SPP_LISTEN);
	tester_set_bit(rp->data, BTP_SPP_SEND_DATA);

	*rsp_len = sizeof(*rp) + 1;

	return BTP_STATUS_SUCCESS;
}

static uint8_t spp_connect(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_spp_connect_cmd *cp = cmd;
	int err;

	LOG_INF("SPP connect");

	err = z_bt_spp_connect((const uint8_t *)&cp->address, 0);
	if (err) {
		LOG_ERR("SPP connect failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t spp_disconnect(const void *cmd, uint16_t cmd_len,
			      void *rsp, uint16_t *rsp_len)
{
	const struct btp_spp_disconnect_cmd *cp = cmd;
	int err;

	LOG_INF("SPP disconnect");

	err = z_bt_spp_disconnect((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("SPP disconnect failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t spp_listen(const void *cmd, uint16_t cmd_len,
			  void *rsp, uint16_t *rsp_len)
{
	const struct btp_spp_listen_cmd *cp = cmd;
	int err;

	LOG_INF("SPP listen: channel=%d", cp->channel);

	err = z_bt_spp_listen(cp->channel);
	if (err) {
		LOG_ERR("SPP listen failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t spp_send_data(const void *cmd, uint16_t cmd_len,
			     void *rsp, uint16_t *rsp_len)
{
	const struct btp_spp_send_data_cmd *cp = cmd;
	int err;

	LOG_INF("SPP send data: len=%d", cp->data_len);

	err = z_bt_spp_send((const uint8_t *)&cp->address,
			    cp->data, cp->data_len);
	if (err) {
		LOG_ERR("SPP send data failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

/* Callbacks from z_api SPP layer */
void btp_spp_connected_cb(const uint8_t *addr)
{
	struct btp_spp_connected_ev ev;

	LOG_INF("SPP connected");

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));

	tester_event(BTP_SERVICE_ID_SPP, BTP_SPP_EV_CONNECTED,
		     &ev, sizeof(ev));
}

void btp_spp_disconnected_cb(const uint8_t *addr)
{
	struct btp_spp_disconnected_ev ev;

	LOG_INF("SPP disconnected");

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));

	tester_event(BTP_SERVICE_ID_SPP, BTP_SPP_EV_DISCONNECTED,
		     &ev, sizeof(ev));
}

static const struct btp_handler handlers[] = {
	{
		.opcode = BTP_SPP_READ_SUPPORTED_COMMANDS,
		.index = BTP_INDEX_NONE,
		.expect_len = 0,
		.func = supported_commands,
	},
	{
		.opcode = BTP_SPP_CONNECT,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_spp_connect_cmd),
		.func = spp_connect,
	},
	{
		.opcode = BTP_SPP_DISCONNECT,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_spp_disconnect_cmd),
		.func = spp_disconnect,
	},
	{
		.opcode = BTP_SPP_LISTEN,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_spp_listen_cmd),
		.func = spp_listen,
	},
	{
		.opcode = BTP_SPP_SEND_DATA,
		.index = BTP_INDEX,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = spp_send_data,
	},
};

uint8_t tester_init_spp(void)
{
	tester_register_command_handlers(BTP_SERVICE_ID_SPP, handlers,
					ARRAY_SIZE(handlers));

	LOG_INF("SPP service initialized");

	return BTP_STATUS_SUCCESS;
}

uint8_t tester_unregister_spp(void)
{
	LOG_INF("SPP service unregistered");

	return BTP_STATUS_SUCCESS;
}
