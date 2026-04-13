/* btp_hfp.c - Bluetooth HFP Tester */

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
#define LOG_MODULE_NAME bttester_hfp
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BTTESTER_LOG_LEVEL);

#include "btp/btp.h"
#include "z_api_port.h"

/* Forward declarations for z_api HFP functions */
extern int z_bt_hfp_connect(const uint8_t *addr);
extern int z_bt_hfp_disconnect(const uint8_t *addr);
extern int z_bt_hfp_answer(const uint8_t *addr);
extern int z_bt_hfp_reject(const uint8_t *addr);
extern int z_bt_hfp_dial(const uint8_t *addr, const char *number);
extern int z_bt_hfp_set_volume(const uint8_t *addr,
			       uint8_t type, uint8_t volume);
extern int z_bt_hfp_send_dtmf(const uint8_t *addr, uint8_t code);

static uint8_t supported_commands(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	struct btp_hfp_read_supported_commands_rp *rp = rsp;

	tester_set_bit(rp->data, BTP_HFP_READ_SUPPORTED_COMMANDS);
	tester_set_bit(rp->data, BTP_HFP_CONNECT);
	tester_set_bit(rp->data, BTP_HFP_DISCONNECT);
	tester_set_bit(rp->data, BTP_HFP_ANSWER_CALL);
	tester_set_bit(rp->data, BTP_HFP_REJECT_CALL);
	tester_set_bit(rp->data, BTP_HFP_DIAL);
	tester_set_bit(rp->data, BTP_HFP_SET_VOLUME);
	tester_set_bit(rp->data, BTP_HFP_SEND_DTMF);

	*rsp_len = sizeof(*rp) + 2;

	return BTP_STATUS_SUCCESS;
}

static uint8_t hfp_connect(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_connect_cmd *cp = cmd;
	int err;

	LOG_INF("HFP connect");

	err = z_bt_hfp_connect((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("HFP connect failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t hfp_disconnect(const void *cmd, uint16_t cmd_len,
			      void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_disconnect_cmd *cp = cmd;
	int err;

	LOG_INF("HFP disconnect");

	err = z_bt_hfp_disconnect((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("HFP disconnect failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t hfp_answer_call(const void *cmd, uint16_t cmd_len,
			       void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_answer_call_cmd *cp = cmd;
	int err;

	LOG_INF("HFP answer call");

	err = z_bt_hfp_answer((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("HFP answer call failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t hfp_reject_call(const void *cmd, uint16_t cmd_len,
			       void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_reject_call_cmd *cp = cmd;
	int err;

	LOG_INF("HFP reject call");

	err = z_bt_hfp_reject((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("HFP reject call failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t hfp_dial(const void *cmd, uint16_t cmd_len,
			void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_dial_cmd *cp = cmd;
	char number[33];
	int err;
	uint8_t len;

	LOG_INF("HFP dial");

	len = cp->number_len;
	if (len > 32) {
		len = 32;
	}
	memcpy(number, cp->number, len);
	number[len] = '\0';

	err = z_bt_hfp_dial((const uint8_t *)&cp->address, number);
	if (err) {
		LOG_ERR("HFP dial failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t hfp_set_volume(const void *cmd, uint16_t cmd_len,
			      void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_set_volume_cmd *cp = cmd;
	int err;

	LOG_INF("HFP set volume: type=%d vol=%d", cp->type, cp->volume);

	err = z_bt_hfp_set_volume((const uint8_t *)&cp->address,
				  cp->type, cp->volume);
	if (err) {
		LOG_ERR("HFP set volume failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t hfp_send_dtmf(const void *cmd, uint16_t cmd_len,
			      void *rsp, uint16_t *rsp_len)
{
	const struct btp_hfp_send_dtmf_cmd *cp = cmd;
	int err;

	LOG_INF("HFP send DTMF: code=%c", cp->code);

	err = z_bt_hfp_send_dtmf((const uint8_t *)&cp->address, cp->code);
	if (err) {
		LOG_ERR("HFP send DTMF failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

/* Callbacks from z_api HFP layer */
void btp_hfp_connected_cb(const uint8_t *addr)
{
	struct btp_hfp_connected_ev ev;

	LOG_INF("HFP connected");

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));

	tester_event(BTP_SERVICE_ID_HFP, BTP_HFP_EV_CONNECTED,
		     &ev, sizeof(ev));
}

void btp_hfp_disconnected_cb(const uint8_t *addr)
{
	struct btp_hfp_disconnected_ev ev;

	LOG_INF("HFP disconnected");

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));

	tester_event(BTP_SERVICE_ID_HFP, BTP_HFP_EV_DISCONNECTED,
		     &ev, sizeof(ev));
}

static const struct btp_handler handlers[] = {
	{
		.opcode = BTP_HFP_READ_SUPPORTED_COMMANDS,
		.index = BTP_INDEX_NONE,
		.expect_len = 0,
		.func = supported_commands,
	},
	{
		.opcode = BTP_HFP_CONNECT,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_hfp_connect_cmd),
		.func = hfp_connect,
	},
	{
		.opcode = BTP_HFP_DISCONNECT,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_hfp_disconnect_cmd),
		.func = hfp_disconnect,
	},
	{
		.opcode = BTP_HFP_ANSWER_CALL,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_hfp_answer_call_cmd),
		.func = hfp_answer_call,
	},
	{
		.opcode = BTP_HFP_REJECT_CALL,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_hfp_reject_call_cmd),
		.func = hfp_reject_call,
	},
	{
		.opcode = BTP_HFP_DIAL,
		.index = BTP_INDEX,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = hfp_dial,
	},
	{
		.opcode = BTP_HFP_SET_VOLUME,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_hfp_set_volume_cmd),
		.func = hfp_set_volume,
	},
	{
		.opcode = BTP_HFP_SEND_DTMF,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_hfp_send_dtmf_cmd),
		.func = hfp_send_dtmf,
	},
};

uint8_t tester_init_hfp(void)
{
	tester_register_command_handlers(BTP_SERVICE_ID_HFP, handlers,
					ARRAY_SIZE(handlers));

	LOG_INF("HFP service initialized");

	return BTP_STATUS_SUCCESS;
}

uint8_t tester_unregister_hfp(void)
{
	LOG_INF("HFP service unregistered");

	return BTP_STATUS_SUCCESS;
}
