/* btp_rfcomm.c - Bluetooth RFCOMM Tester */

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
#define LOG_MODULE_NAME bttester_rfcomm
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BTTESTER_LOG_LEVEL);

#include "btp/btp.h"
#include "z_api_port.h"

/* Default RFCOMM server channel */
#define RFCOMM_DEFAULT_CHANNEL 5

static uint8_t rfcomm_channel = RFCOMM_DEFAULT_CHANNEL;

static uint8_t supported_commands(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	struct btp_rfcomm_read_supported_commands_rp *rp = rsp;

	tester_set_bit(rp->data, BTP_RFCOMM_READ_SUPPORTED_COMMANDS);
	tester_set_bit(rp->data, BTP_RFCOMM_CONNECT);
	tester_set_bit(rp->data, BTP_RFCOMM_DISCONNECT);
	tester_set_bit(rp->data, BTP_RFCOMM_LISTEN);
	tester_set_bit(rp->data, BTP_RFCOMM_SEND_DATA);

	*rsp_len = sizeof(*rp) + 1;

	return BTP_STATUS_SUCCESS;
}

/* Forward declarations for z_api SPP functions */
extern int z_bt_spp_connect(const uint8_t *addr, uint8_t channel);
extern int z_bt_spp_disconnect(const uint8_t *addr);
extern int z_bt_spp_listen(uint8_t channel);
extern int z_bt_spp_send(const uint8_t *addr, const uint8_t *data, uint16_t len);

static uint8_t rfcomm_connect(const void *cmd, uint16_t cmd_len,
			      void *rsp, uint16_t *rsp_len)
{
	const struct btp_rfcomm_connect_cmd *cp = cmd;
	int err;

	LOG_INF("RFCOMM connect: channel=%d", cp->channel);

	rfcomm_channel = cp->channel;
	err = z_bt_spp_connect((const uint8_t *)&cp->address, cp->channel);
	if (err) {
		LOG_ERR("RFCOMM connect failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t rfcomm_disconnect(const void *cmd, uint16_t cmd_len,
				 void *rsp, uint16_t *rsp_len)
{
	const struct btp_rfcomm_disconnect_cmd *cp = cmd;
	int err;

	LOG_INF("RFCOMM disconnect");

	err = z_bt_spp_disconnect((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("RFCOMM disconnect failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t rfcomm_listen(const void *cmd, uint16_t cmd_len,
			     void *rsp, uint16_t *rsp_len)
{
	const struct btp_rfcomm_listen_cmd *cp = cmd;
	int err;

	rfcomm_channel = cp->channel;

	LOG_INF("RFCOMM listen: channel=%d", rfcomm_channel);

	err = z_bt_spp_listen(rfcomm_channel);
	if (err) {
		LOG_ERR("RFCOMM listen failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t rfcomm_send_data(const void *cmd, uint16_t cmd_len,
				void *rsp, uint16_t *rsp_len)
{
	const struct btp_rfcomm_send_data_cmd *cp = cmd;
	int err;

	LOG_INF("RFCOMM send data: len=%d", cp->data_len);

	err = z_bt_spp_send((const uint8_t *)&cp->address,
			    cp->data, cp->data_len);
	if (err) {
		LOG_ERR("RFCOMM send data failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

/* Callback from z_api SPP layer for connection state changes */
void btp_rfcomm_connected_cb(const uint8_t *addr, uint8_t channel)
{
	struct btp_rfcomm_connected_ev ev;

	LOG_INF("RFCOMM connected: channel=%d", channel);

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));
	ev.channel = channel;

	tester_event(BTP_SERVICE_ID_RFCOMM, BTP_RFCOMM_EV_CONNECTED,
		     &ev, sizeof(ev));
}

void btp_rfcomm_disconnected_cb(const uint8_t *addr)
{
	struct btp_rfcomm_disconnected_ev ev;

	LOG_INF("RFCOMM disconnected");

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));

	tester_event(BTP_SERVICE_ID_RFCOMM, BTP_RFCOMM_EV_DISCONNECTED,
		     &ev, sizeof(ev));
}

void btp_rfcomm_data_received_cb(const uint8_t *addr,
				 const uint8_t *data, uint16_t len)
{
	struct btp_rfcomm_data_received_ev *ev;
	uint8_t buf[sizeof(*ev) + BTP_DATA_MAX_SIZE];

	LOG_INF("RFCOMM data received: len=%d", len);

	if (len > BTP_DATA_MAX_SIZE) {
		len = BTP_DATA_MAX_SIZE;
	}

	ev = (struct btp_rfcomm_data_received_ev *)buf;
	memcpy(&ev->address, addr, sizeof(bt_addr_le_t));
	ev->data_len = len;
	memcpy(ev->data, data, len);

	tester_event(BTP_SERVICE_ID_RFCOMM, BTP_RFCOMM_EV_DATA_RECEIVED,
		     buf, sizeof(*ev) + len);
}

static const struct btp_handler handlers[] = {
	{
		.opcode = BTP_RFCOMM_READ_SUPPORTED_COMMANDS,
		.index = BTP_INDEX_NONE,
		.expect_len = 0,
		.func = supported_commands,
	},
	{
		.opcode = BTP_RFCOMM_CONNECT,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_rfcomm_connect_cmd),
		.func = rfcomm_connect,
	},
	{
		.opcode = BTP_RFCOMM_DISCONNECT,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_rfcomm_disconnect_cmd),
		.func = rfcomm_disconnect,
	},
	{
		.opcode = BTP_RFCOMM_LISTEN,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_rfcomm_listen_cmd),
		.func = rfcomm_listen,
	},
	{
		.opcode = BTP_RFCOMM_SEND_DATA,
		.index = BTP_INDEX,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = rfcomm_send_data,
	},
};

uint8_t tester_init_rfcomm(void)
{
	tester_register_command_handlers(BTP_SERVICE_ID_RFCOMM, handlers,
					ARRAY_SIZE(handlers));

	LOG_INF("RFCOMM service initialized");

	return BTP_STATUS_SUCCESS;
}

uint8_t tester_unregister_rfcomm(void)
{
	LOG_INF("RFCOMM service unregistered");

	return BTP_STATUS_SUCCESS;
}
