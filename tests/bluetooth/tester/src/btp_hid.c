/* btp_hid.c - Bluetooth HID Tester */

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
#define LOG_MODULE_NAME bttester_hid
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BTTESTER_LOG_LEVEL);

#include "btp/btp.h"
#include "z_api_port.h"

/* Forward declarations for z_api HID functions */
extern int z_bt_hid_register(uint8_t sub_class);
extern int z_bt_hid_connect(const uint8_t *addr);
extern int z_bt_hid_disconnect(const uint8_t *addr);
extern int z_bt_hid_send_report(const uint8_t *addr, uint8_t report_id,
				const uint8_t *data, uint16_t len);

static uint8_t supported_commands(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	struct btp_hid_read_supported_commands_rp *rp = rsp;

	tester_set_bit(rp->data, BTP_HID_READ_SUPPORTED_COMMANDS);
	tester_set_bit(rp->data, BTP_HID_CONNECT);
	tester_set_bit(rp->data, BTP_HID_DISCONNECT);
	tester_set_bit(rp->data, BTP_HID_REGISTER_DEVICE);
	tester_set_bit(rp->data, BTP_HID_SEND_REPORT);

	*rsp_len = sizeof(*rp) + 1;

	return BTP_STATUS_SUCCESS;
}

static uint8_t hid_connect(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hid_connect_cmd *cp = cmd;
	int err;

	LOG_INF("HID connect");

	err = z_bt_hid_connect((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("HID connect failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t hid_disconnect(const void *cmd, uint16_t cmd_len,
			      void *rsp, uint16_t *rsp_len)
{
	const struct btp_hid_disconnect_cmd *cp = cmd;
	int err;

	LOG_INF("HID disconnect");

	err = z_bt_hid_disconnect((const uint8_t *)&cp->address);
	if (err) {
		LOG_ERR("HID disconnect failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t hid_register_device(const void *cmd, uint16_t cmd_len,
				   void *rsp, uint16_t *rsp_len)
{
	const struct btp_hid_register_device_cmd *cp = cmd;
	int err;

	LOG_INF("HID register device: sub_class=%d", cp->sub_class);

	err = z_bt_hid_register(cp->sub_class);
	if (err) {
		LOG_ERR("HID register device failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t hid_send_report(const void *cmd, uint16_t cmd_len,
			       void *rsp, uint16_t *rsp_len)
{
	const struct btp_hid_send_report_cmd *cp = cmd;
	int err;

	LOG_INF("HID send report: id=%d len=%d", cp->report_id, cp->data_len);

	err = z_bt_hid_send_report((const uint8_t *)&cp->address,
				   cp->report_id, cp->data, cp->data_len);
	if (err) {
		LOG_ERR("HID send report failed: %d", err);
		return BTP_STATUS_FAILED;
	}

	return BTP_STATUS_SUCCESS;
}

/* Callbacks from z_api HID layer */
void btp_hid_connected_cb(const uint8_t *addr)
{
	struct btp_hid_connected_ev ev;

	LOG_INF("HID connected");

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));

	tester_event(BTP_SERVICE_ID_HID, BTP_HID_EV_CONNECTED,
		     &ev, sizeof(ev));
}

void btp_hid_disconnected_cb(const uint8_t *addr)
{
	struct btp_hid_disconnected_ev ev;

	LOG_INF("HID disconnected");

	memcpy(&ev.address, addr, sizeof(bt_addr_le_t));

	tester_event(BTP_SERVICE_ID_HID, BTP_HID_EV_DISCONNECTED,
		     &ev, sizeof(ev));
}

static const struct btp_handler handlers[] = {
	{
		.opcode = BTP_HID_READ_SUPPORTED_COMMANDS,
		.index = BTP_INDEX_NONE,
		.expect_len = 0,
		.func = supported_commands,
	},
	{
		.opcode = BTP_HID_CONNECT,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_hid_connect_cmd),
		.func = hid_connect,
	},
	{
		.opcode = BTP_HID_DISCONNECT,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_hid_disconnect_cmd),
		.func = hid_disconnect,
	},
	{
		.opcode = BTP_HID_REGISTER_DEVICE,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_hid_register_device_cmd),
		.func = hid_register_device,
	},
	{
		.opcode = BTP_HID_SEND_REPORT,
		.index = BTP_INDEX,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = hid_send_report,
	},
};

uint8_t tester_init_hid(void)
{
	tester_register_command_handlers(BTP_SERVICE_ID_HID, handlers,
					ARRAY_SIZE(handlers));

	LOG_INF("HID service initialized");

	return BTP_STATUS_SUCCESS;
}

uint8_t tester_unregister_hid(void)
{
	LOG_INF("HID service unregistered");

	return BTP_STATUS_SUCCESS;
}
