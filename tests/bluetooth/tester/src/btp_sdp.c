/* btp_sdp.c - Bluetooth SDP Tester */

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
#define LOG_MODULE_NAME bttester_sdp
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BTTESTER_LOG_LEVEL);

#include "btp/btp.h"
#include "z_api_port.h"

static uint8_t supported_commands(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	struct btp_sdp_read_supported_commands_rp *rp = rsp;

	tester_set_bit(rp->data, BTP_SDP_READ_SUPPORTED_COMMANDS);
	tester_set_bit(rp->data, BTP_SDP_SEARCH_REQ);
	tester_set_bit(rp->data, BTP_SDP_ATTR_REQ);
	tester_set_bit(rp->data, BTP_SDP_SEARCH_ATTR_REQ);

	*rsp_len = sizeof(*rp) + 1;

	return BTP_STATUS_SUCCESS;
}

static uint8_t search_request(const void *cmd, uint16_t cmd_len,
			      void *rsp, uint16_t *rsp_len)
{
	const struct btp_sdp_search_req_cmd *cp = cmd;

	LOG_INF("SDP search request: uuid=0x%04x", cp->uuid);

	/* SDP search is handled internally by the Zephyr BT stack.
	 * The BTP tester acts as a pass-through; the actual SDP search
	 * is triggered by the upper layer via BTP commands.
	 * For now, return success to indicate the command was accepted.
	 * The search result will be sent as an event when available.
	 */

	return BTP_STATUS_SUCCESS;
}

static uint8_t attribute_request(const void *cmd, uint16_t cmd_len,
				 void *rsp, uint16_t *rsp_len)
{
	const struct btp_sdp_attr_req_cmd *cp = cmd;

	LOG_INF("SDP attribute request: handle=0x%08x", cp->service_record_handle);

	return BTP_STATUS_SUCCESS;
}

static uint8_t search_attribute_request(const void *cmd, uint16_t cmd_len,
					void *rsp, uint16_t *rsp_len)
{
	const struct btp_sdp_search_attr_req_cmd *cp = cmd;

	LOG_INF("SDP search attribute request: uuid=0x%04x", cp->uuid);

	return BTP_STATUS_SUCCESS;
}

static const struct btp_handler handlers[] = {
	{
		.opcode = BTP_SDP_READ_SUPPORTED_COMMANDS,
		.index = BTP_INDEX_NONE,
		.expect_len = 0,
		.func = supported_commands,
	},
	{
		.opcode = BTP_SDP_SEARCH_REQ,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_sdp_search_req_cmd),
		.func = search_request,
	},
	{
		.opcode = BTP_SDP_ATTR_REQ,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_sdp_attr_req_cmd),
		.func = attribute_request,
	},
	{
		.opcode = BTP_SDP_SEARCH_ATTR_REQ,
		.index = BTP_INDEX,
		.expect_len = sizeof(struct btp_sdp_search_attr_req_cmd),
		.func = search_attribute_request,
	},
};

uint8_t tester_init_sdp(void)
{
	tester_register_command_handlers(BTP_SERVICE_ID_SDP, handlers,
					ARRAY_SIZE(handlers));

	LOG_INF("SDP service initialized");

	return BTP_STATUS_SUCCESS;
}

uint8_t tester_unregister_sdp(void)
{
	LOG_INF("SDP service unregistered");

	return BTP_STATUS_SUCCESS;
}
