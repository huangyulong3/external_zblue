/****************************************************************************
 *  Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ***************************************************************************/

/*
 * BR/EDR Upper Link Layer (ULL) Implementation
 * Bluetooth Core Spec Vol 2, Part C: Link Manager Protocol
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/buf.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>

#include "hal/cpu.h"
#include "hal/ticker.h"

#include "util/util.h"
#include "util/mem.h"
#include "util/memq.h"
#include "util/mayfly.h"

#include "ticker/ticker.h"

#include "pdu_bredr.h"
#include "lll_bredr.h"
#include "ull_bredr.h"
#include "lmp_internal.h"
#include "lmp_proc.h"
#include "ticker_bredr.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_ctlr_ull_bredr, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

/* Forward declarations */
static struct lmp_tx_node *lmp_tx_acquire(void);
static void lmp_tx_node_release(struct lmp_tx_node *node);

/* Connection pool */
static struct ull_bredr_conn conn_pool[ULL_BREDR_MAX_CONN];
static void *conn_free;

/* SCO pool */
static struct ull_bredr_sco sco_pool[ULL_BREDR_SCO_MAX];
static void *sco_free;

/* eSCO pool */
static struct ull_bredr_esco esco_pool[ULL_BREDR_ESCO_MAX];
static void *esco_free;

/* LMP TX node pool */
#define LMP_TX_POOL_SIZE  16
static struct lmp_tx_node lmp_tx_pool[LMP_TX_POOL_SIZE];
static void *lmp_tx_free;

/* Inquiry context */
static struct ull_bredr_inquiry inquiry_ctx;

/* Inquiry scan context */
static struct ull_bredr_inquiry_scan inquiry_scan_ctx;

/* Page context */
static struct ull_bredr_page page_ctx;

/* Page scan context */
static struct ull_bredr_page_scan page_scan_ctx;

/* LM Environment */
static struct ull_bredr_lm_env lm_env;

/* Local BD_ADDR */
static uint8_t local_bd_addr[6];

/* Class of Device */
static uint8_t class_of_device[3];

/* Local name */
static uint8_t local_name[248];
static uint8_t local_name_len;

/* Local features */
static uint8_t local_features[8];
static uint8_t local_ext_features[8];

/* Default local name */
#define BT_DEFAULT_LOCALNAME "Zephyr BR/EDR"

/*
 * Initialization
 */
int ull_bredr_init(void)
{
	int err;

	/* Initialize connection pool */
	mem_init(conn_pool, sizeof(struct ull_bredr_conn),
		 ULL_BREDR_MAX_CONN, &conn_free);

	/* Initialize SCO pool */
	mem_init(sco_pool, sizeof(struct ull_bredr_sco),
		 ULL_BREDR_SCO_MAX, &sco_free);

	/* Initialize eSCO pool */
	mem_init(esco_pool, sizeof(struct ull_bredr_esco),
		 ULL_BREDR_ESCO_MAX, &esco_free);

	/* Initialize LMP TX pool */
	mem_init(lmp_tx_pool, sizeof(struct lmp_tx_node),
		 LMP_TX_POOL_SIZE, &lmp_tx_free);

	/* Initialize LM environment */
	memset(&lm_env, 0, sizeof(lm_env));

	/* Initialize HCI configuration parameters */
	lm_env.hci.con_accept_to = CON_ACCEPT_TO_DFT;
	lm_env.hci.page_to = PAGE_TO_DFT;
	lm_env.hci.inq_scan_intv = INQ_SCAN_INTV_DFT;
	lm_env.hci.inq_scan_win = INQ_SCAN_WIN_DFT;
	lm_env.hci.page_scan_intv = PAGE_SCAN_INTV_DFT;
	lm_env.hci.page_scan_win = PAGE_SCAN_WIN_DFT;
	lm_env.hci.page_scan_rep_mode = 1;  /* R1 */
	lm_env.hci.inq_tx_pwr_lvl = 0;
	lm_env.hci.iac_lap[0] = 0x33;  /* GIAC */
	lm_env.hci.iac_lap[1] = 0x8B;
	lm_env.hci.iac_lap[2] = 0x9E;
	lm_env.sam_info.active_index = SAM_DISABLED;

	/* Initialize Host channel classification */
	memset(&lm_env.afh.host_ch_class[0], 0xFF, 10);
	memset(&lm_env.afh.master_ch_map[0], 0xFF, 10);
	lm_env.afh.master_ch_map[9] &= ~(0x80);  /* Channel 79 not used */

	/* Initialize local name */
	local_name_len = strlen(BT_DEFAULT_LOCALNAME);
	memcpy(local_name, BT_DEFAULT_LOCALNAME, local_name_len);

	/* Initialize LLL */
	err = lll_bredr_init();
	if (err) {
		return err;
	}

	LOG_INF("BR/EDR Controller initialized");

	return 0;
}

int ull_bredr_reset(void)
{
	int err;

	/* Stop any ongoing operations */
	ull_bredr_inquiry_stop();
	ull_bredr_page_stop();
	ull_bredr_inquiry_scan_enable(0);
	ull_bredr_page_scan_enable(0);

	/* Disconnect all connections */
	for (int i = 0; i < ULL_BREDR_MAX_CONN; i++) {
		if (conn_pool[i].lll.handle != 0xFFFF) {
			ull_bredr_conn_cleanup(&conn_pool[i],
					       BT_HCI_ERR_REMOTE_POWER_OFF);
		}
	}

	/* Re-initialize pools */
	mem_init(conn_pool, sizeof(struct ull_bredr_conn),
		 ULL_BREDR_MAX_CONN, &conn_free);
	mem_init(sco_pool, sizeof(struct ull_bredr_sco),
		 ULL_BREDR_SCO_MAX, &sco_free);
	mem_init(esco_pool, sizeof(struct ull_bredr_esco),
		 ULL_BREDR_ESCO_MAX, &esco_free);

	/* Reset LLL */
	err = lll_bredr_reset();
	if (err) {
		return err;
	}

	return 0;
}

/*
 * Inquiry Operations
 */
int ull_bredr_inquiry_start(uint8_t *lap, uint8_t length, uint8_t num_responses)
{
	struct lll_bredr_inquiry *lll = &inquiry_ctx.lll;
	uint32_t ticks_anchor;
	uint32_t ticks_slot;
	int err;

	if (lll->state != 0) {
		return -EALREADY;
	}

	/* Set inquiry parameters */
	memcpy(lll->lap, lap, 3);
	lll->inquiry_length = length;
	lll->num_responses = num_responses;
	lll->current_responses = 0;
	lll->train = 0;
	lll->n_inquiry = LLL_BREDR_NINQUIRY_DEFAULT;

	/* Initialize LLL header */
	lll_hdr_init(&lll->hdr, &inquiry_ctx.ull);

	/* Calculate ticker parameters */
	ticks_anchor = ticker_ticks_now_get();
	ticks_slot = ticker_bredr_us_to_ticks(ticker_bredr_scan_evt_dur_get());

	/* Start inquiry ticker */
	err = ticker_bredr_inquiry_start(&inquiry_ctx, ticks_anchor, ticks_slot);
	if (err) {
		return err;
	}

	lll->state = 1;

	LOG_DBG("Inquiry started: LAP=%02x%02x%02x, length=%u",
		lap[2], lap[1], lap[0], length);

	return 0;
}

int ull_bredr_inquiry_stop(void)
{
	struct lll_bredr_inquiry *lll = &inquiry_ctx.lll;
	int err;

	if (lll->state == 0) {
		return -EALREADY;
	}

	/* Stop inquiry ticker */
	err = ticker_bredr_inquiry_stop();
	if (err && err != -EALREADY) {
		LOG_WRN("Failed to stop inquiry ticker: %d", err);
	}

	lll->state = 0;

	LOG_DBG("Inquiry stopped");

	return 0;
}

int ull_bredr_inquiry_scan_enable(uint8_t enable)
{
	struct lll_bredr_inquiry_scan *lll = &inquiry_scan_ctx.lll;
	int err;

	if (enable) {
		if (lll->state != 0) {
			return -EALREADY;
		}

		/* Set local BD_ADDR and CoD */
		memcpy(lll->bd_addr, local_bd_addr, 6);
		memcpy(lll->class_of_device, class_of_device, 3);

		/* Initialize LLL header */
		lll_hdr_init(&lll->hdr, &inquiry_scan_ctx.ull);

		/* Start inquiry scan ticker */
		err = ticker_bredr_inquiry_scan_start(&inquiry_scan_ctx,
						      lm_env.hci.inq_scan_intv,
						      lm_env.hci.inq_scan_win);
		if (err) {
			return err;
		}

		lll->state = 1;
		LOG_DBG("Inquiry scan enabled");
	} else {
		if (lll->state == 0) {
			return -EALREADY;
		}

		/* Stop inquiry scan ticker */
		err = ticker_bredr_inquiry_scan_stop();
		if (err && err != -EALREADY) {
			LOG_WRN("Failed to stop inquiry scan ticker: %d", err);
		}

		lll->state = 0;
		LOG_DBG("Inquiry scan disabled");
	}

	return 0;
}

int ull_bredr_inquiry_scan_set_params(uint16_t interval, uint16_t window)
{
	struct lll_bredr_inquiry_scan *lll = &inquiry_scan_ctx.lll;

	lll->scan_interval = interval;
	lll->scan_window = window;

	return 0;
}

/*
 * Page Operations
 */
int ull_bredr_page_start(uint8_t *bd_addr, uint8_t page_scan_rep_mode,
			 uint16_t clock_offset)
{
	struct lll_bredr_page *lll = &page_ctx.lll;
	uint32_t ticks_anchor;
	uint32_t ticks_slot;
	int err;

	if (lll->state != 0) {
		return -EALREADY;
	}

	/* Set page parameters */
	memcpy(lll->bd_addr, bd_addr, 6);
	lll->page_scan_rep_mode = page_scan_rep_mode;
	lll->clock_offset = clock_offset;
	lll->train = 0;
	lll->n_page = LLL_BREDR_NPAGE_DEFAULT;

	/* Initialize LLL header */
	lll_hdr_init(&lll->hdr, &page_ctx.ull);

	/* Calculate ticker parameters */
	ticks_anchor = ticker_ticks_now_get();
	ticks_slot = ticker_bredr_us_to_ticks(ticker_bredr_scan_evt_dur_get());

	/* Start page ticker */
	err = ticker_bredr_page_start(&page_ctx, ticks_anchor, ticks_slot);
	if (err) {
		return err;
	}

	lll->state = 1;

	LOG_DBG("Page started: BD_ADDR=%02x:%02x:%02x:%02x:%02x:%02x",
		bd_addr[5], bd_addr[4], bd_addr[3],
		bd_addr[2], bd_addr[1], bd_addr[0]);

	return 0;
}

int ull_bredr_page_stop(void)
{
	struct lll_bredr_page *lll = &page_ctx.lll;
	int err;

	if (lll->state == 0) {
		return -EALREADY;
	}

	/* Stop page ticker */
	err = ticker_bredr_page_stop();
	if (err && err != -EALREADY) {
		LOG_WRN("Failed to stop page ticker: %d", err);
	}

	lll->state = 0;

	LOG_DBG("Page stopped");

	return 0;
}

int ull_bredr_page_scan_enable(uint8_t enable)
{
	struct lll_bredr_page_scan *lll = &page_scan_ctx.lll;
	int err;

	if (enable) {
		if (lll->state != 0) {
			return -EALREADY;
		}

		/* Initialize LLL header */
		lll_hdr_init(&lll->hdr, &page_scan_ctx.ull);

		/* Start page scan ticker */
		err = ticker_bredr_page_scan_start(&page_scan_ctx,
						   lm_env.hci.page_scan_intv,
						   lm_env.hci.page_scan_win);
		if (err) {
			return err;
		}

		lll->state = 1;
		LOG_DBG("Page scan enabled");
	} else {
		if (lll->state == 0) {
			return -EALREADY;
		}

		/* Stop page scan ticker */
		err = ticker_bredr_page_scan_stop();
		if (err && err != -EALREADY) {
			LOG_WRN("Failed to stop page scan ticker: %d", err);
		}

		lll->state = 0;
		LOG_DBG("Page scan disabled");
	}

	return 0;
}

int ull_bredr_page_scan_set_params(uint16_t interval, uint16_t window)
{
	struct lll_bredr_page_scan *lll = &page_scan_ctx.lll;

	lll->scan_interval = interval;
	lll->scan_window = window;

	return 0;
}

/*
 * Connection Management
 */
struct ull_bredr_conn *ull_bredr_conn_acquire(void)
{
	struct ull_bredr_conn *conn;

	conn = mem_acquire(&conn_free);
	if (conn) {
		memset(conn, 0, sizeof(*conn));
		conn->lll.handle = 0xFFFF;
	}

	return conn;
}

void ull_bredr_conn_release(struct ull_bredr_conn *conn)
{
	mem_release(conn, &conn_free);
}

struct ull_bredr_conn *ull_bredr_conn_get(uint16_t handle)
{
	if (handle >= ULL_BREDR_MAX_CONN) {
		return NULL;
	}

	struct ull_bredr_conn *conn = &conn_pool[handle];

	if (conn->lll.handle != handle) {
		return NULL;
	}

	return conn;
}

struct ull_bredr_conn *ull_bredr_conn_get_by_addr(uint8_t *bd_addr)
{
	for (int i = 0; i < ULL_BREDR_MAX_CONN; i++) {
		if (conn_pool[i].lll.handle != 0xFFFF &&
		    memcmp(conn_pool[i].lll.bd_addr, bd_addr, 6) == 0) {
			return &conn_pool[i];
		}
	}

	return NULL;
}

uint16_t ull_bredr_conn_handle_get(struct ull_bredr_conn *conn)
{
	return mem_index_get(conn, conn_pool, sizeof(struct ull_bredr_conn));
}

void ull_bredr_conn_setup(struct ull_bredr_conn *conn, uint8_t role,
			  uint8_t *bd_addr, uint8_t lt_addr)
{
	struct lll_bredr_conn *lll = &conn->lll;
	uint8_t link_id;

	/* Assign handle */
	lll->handle = ull_bredr_conn_handle_get(conn);
	lll->role = role;
	lll->lt_addr = lt_addr;
	memcpy(lll->bd_addr, bd_addr, 6);

	/* Initialize connection parameters */
	lll->poll_interval = BREDR_TPOLL_DEFAULT;
	lll->supervision_timeout = LSTO_DFT;
	lll->max_slots = 5;
	lll->tx_seqn = 0;
	lll->rx_seqn = 0;
	lll->tx_arqn = 1;
	lll->flow = 1;

	/* Initialize LLL header */
	lll_hdr_init(&lll->hdr, &conn->ull);

	/* Initialize LC state */
	conn->lc_state = BREDR_LC_CONNECTED;

	/* Initialize link structure */
	conn->link.current_mode = LM_ACTIVE_MODE;
	conn->link.initiator = true;
	conn->link.acl_packet_type = 0x0008 | 0x0010 | 0x0400 | 0x0800 |
				     0x4000 | 0x8000;  /* DM1, DH1, DM3, DH3, DM5, DH5 */
	conn->link.tx_max_slot_cur = 0x01;
	conn->link.cur_packet_type_table = PACKET_TABLE_1MBPS;
	conn->link.service_type = QOS_BEST_EFFORT;
	conn->link.failed_contact = 0;
	conn->link.auth_payl_to = AUTH_PAYL_TO_DFT;
	conn->link.link_timeout = LSTO_DFT;
	conn->link.poll_interval = POLL_INTERVAL_DFT;
	conn->link.role = role;
	conn->link.lt_addr = lt_addr;

	/* Initialize SAM info */
	conn->sam_info.rem_idx = SAM_DISABLED;
	conn->sam_info.loc_idx = SAM_DISABLED;
	conn->sam_info.rem_tx_av = 0xFF;
	conn->sam_info.rem_rx_av = 0xFF;
	conn->sam_info.loc_tx_av = 0xFF;
	conn->sam_info.loc_rx_av = 0xFF;

	/* Initialize info structure */
	memcpy(conn->info.bd_addr, bd_addr, 6);
	memcpy(conn->info.local_bd_addr, local_bd_addr, 6);

	/* Initialize AFH channel map */
	memset(&conn->afh.ch_map[0], 0xFF, 10);
	conn->afh.ch_map[9] &= ~(0x80);

	/* Initialize LMP pending lists */
	sys_slist_init(&conn->lmp_tx_pending);
	sys_slist_init(&conn->lmp_rx_pending);

	/* Update LM environment */
	link_id = lll->handle;
	if (link_id < ULL_BREDR_MAX_CONN) {
		memcpy(lm_env.con_info[link_id].bd_addr, bd_addr, 6);
		lm_env.con_info[link_id].state = BREDR_LINK_CONNECTED;
		lm_env.con_info[link_id].role = role;
		lm_env.con_info[link_id].lt_addr = lt_addr;
	}

	/* Start connection ticker */
	{
		uint32_t ticks_anchor = ticker_ticks_now_get();
		uint32_t ticks_slot = ticker_bredr_us_to_ticks(
			ticker_bredr_acl_evt_dur_get());
		int err;

		err = ticker_bredr_conn_start(lll->handle, conn, ticks_anchor,
					      ticks_slot, lll->poll_interval);
		if (err) {
			LOG_ERR("Failed to start connection ticker: %d", err);
		}
	}

	LOG_INF("Connection setup: handle=%u, role=%s, BD_ADDR=%02x:%02x:%02x:%02x:%02x:%02x",
		lll->handle, role ? "peripheral" : "central",
		bd_addr[5], bd_addr[4], bd_addr[3],
		bd_addr[2], bd_addr[1], bd_addr[0]);
}

void ull_bredr_conn_cleanup(struct ull_bredr_conn *conn, uint8_t reason)
{
	struct lll_bredr_conn *lll = &conn->lll;
	uint8_t link_id;
	int err;

	if (lll->handle == 0xFFFF) {
		return;
	}

	link_id = lll->handle;

	LOG_INF("Connection cleanup: handle=%u, reason=0x%02x",
		lll->handle, reason);

	/* Stop connection ticker */
	err = ticker_bredr_conn_stop(link_id);
	if (err && err != -EALREADY) {
		LOG_WRN("Failed to stop connection ticker: %d", err);
	}

	/* Update LM environment */
	if (link_id < ULL_BREDR_MAX_CONN) {
		lm_env.con_info[link_id].state = BREDR_LINK_FREE;
		if (lm_env.con_info[link_id].role == BREDR_ROLE_CENTRAL) {
			ull_bredr_lt_addr_free(lm_env.con_info[link_id].lt_addr);
		}
	}

	/* Notify disconnect callback */
	if (conn->disconnect_cb) {
		conn->disconnect_cb(reason);
	}

	/* Mark as invalid */
	lll->handle = 0xFFFF;

	/* Release connection */
	ull_bredr_conn_release(conn);
}

int ull_bredr_conn_disconnect(uint16_t handle, uint8_t reason)
{
	struct ull_bredr_conn *conn = ull_bredr_conn_get(handle);

	if (!conn) {
		return -ENOENT;
	}

	/* Send LMP_detach */
	int err = ull_bredr_lmp_detach(conn, reason);
	if (err) {
		return err;
	}

	return 0;
}

/*
 * LMP Procedures
 */
int ull_bredr_lmp_version_req(struct ull_bredr_conn *conn)
{
	struct pdu_lmp_version pdu;

	pdu.hdr.tid = 0;
	pdu.hdr.opcode = LMP_VERSION_REQ;
	pdu.vers_nr = 0x0C;  /* Bluetooth 6.0 */
	pdu.comp_id = sys_cpu_to_le16(0xFFFF);  /* Company ID */
	pdu.sub_vers_nr = sys_cpu_to_le16(0x0001);

	/* Queue LMP PDU for transmission */
	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

int ull_bredr_lmp_features_req(struct ull_bredr_conn *conn)
{
	struct pdu_lmp_features pdu;

	pdu.hdr.tid = 0;
	pdu.hdr.opcode = LMP_FEATURES_REQ;
	memcpy(pdu.features, local_features, 8);

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

int ull_bredr_lmp_name_req(struct ull_bredr_conn *conn, uint8_t offset)
{
	struct pdu_lmp_name_req pdu;

	pdu.hdr.tid = 0;
	pdu.hdr.opcode = LMP_NAME_REQ;
	pdu.name_offset = offset;

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

int ull_bredr_lmp_detach(struct ull_bredr_conn *conn, uint8_t reason)
{
	struct pdu_lmp_detach pdu;

	pdu.hdr.tid = 0;
	pdu.hdr.opcode = LMP_DETACH;
	pdu.reason = reason;

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

int ull_bredr_lmp_host_conn_req(struct ull_bredr_conn *conn)
{
	struct pdu_lmp_host_conn_req pdu;

	pdu.hdr.tid = 0;
	pdu.hdr.opcode = LMP_HOST_CONNECTION_REQ;

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

int ull_bredr_lmp_setup_complete(struct ull_bredr_conn *conn)
{
	struct pdu_lmp_setup_complete pdu;

	pdu.hdr.tid = 0;
	pdu.hdr.opcode = LMP_SETUP_COMPLETE;

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

/*
 * LMP RX Handling
 */
void ull_bredr_lmp_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;
	uint8_t opcode = hdr->opcode;

	LOG_DBG("LMP RX: opcode=0x%02x, tid=%u", opcode, hdr->tid);

	switch (opcode) {
	case LMP_VERSION_REQ: {
		struct pdu_lmp_version *req = (struct pdu_lmp_version *)pdu;
		struct pdu_lmp_version res;

		/* Store remote version */
		conn->remote_version = req->vers_nr;
		conn->remote_company_id = sys_le16_to_cpu(req->comp_id);
		conn->remote_subversion = sys_le16_to_cpu(req->sub_vers_nr);

		/* Send response */
		res.hdr.tid = hdr->tid;
		res.hdr.opcode = LMP_VERSION_RES;
		res.vers_nr = 0x0C;
		res.comp_id = sys_cpu_to_le16(0xFFFF);
		res.sub_vers_nr = sys_cpu_to_le16(0x0001);

		ull_bredr_tx_enqueue(conn, &res, sizeof(res));
		break;
	}

	case LMP_VERSION_RES: {
		struct pdu_lmp_version *res = (struct pdu_lmp_version *)pdu;

		conn->remote_version = res->vers_nr;
		conn->remote_company_id = sys_le16_to_cpu(res->comp_id);
		conn->remote_subversion = sys_le16_to_cpu(res->sub_vers_nr);

		LOG_DBG("Remote version: %u, company=0x%04x, subver=0x%04x",
			conn->remote_version, conn->remote_company_id,
			conn->remote_subversion);
		break;
	}

	case LMP_FEATURES_REQ: {
		struct pdu_lmp_features *req = (struct pdu_lmp_features *)pdu;
		struct pdu_lmp_features res;

		memcpy(conn->remote_features, req->features, 8);

		res.hdr.tid = hdr->tid;
		res.hdr.opcode = LMP_FEATURES_RES;
		memcpy(res.features, local_features, 8);

		ull_bredr_tx_enqueue(conn, &res, sizeof(res));
		break;
	}

	case LMP_FEATURES_RES: {
		struct pdu_lmp_features *res = (struct pdu_lmp_features *)pdu;

		memcpy(conn->remote_features, res->features, 8);
		LOG_DBG("Remote features received");
		break;
	}

	case LMP_NAME_REQ: {
		struct pdu_lmp_name_req *req = (struct pdu_lmp_name_req *)pdu;
		struct pdu_lmp_name_res res;
		uint8_t offset = req->name_offset;
		uint8_t frag_len;

		res.hdr.tid = hdr->tid;
		res.hdr.opcode = LMP_NAME_RES;
		res.name_offset = offset;
		res.name_length = local_name_len;

		frag_len = MIN(14, local_name_len - offset);
		memcpy(res.name_frag, &local_name[offset], frag_len);

		ull_bredr_tx_enqueue(conn, &res,
				     offsetof(struct pdu_lmp_name_res, name_frag) + frag_len);
		break;
	}

	case LMP_NAME_RES: {
		struct pdu_lmp_name_res *res = (struct pdu_lmp_name_res *)pdu;
		uint8_t offset = res->name_offset;
		uint8_t frag_len = MIN(14, res->name_length - offset);

		memcpy(&conn->remote_name[offset], res->name_frag, frag_len);
		conn->remote_name_len = res->name_length;

		/* Request next fragment if needed */
		if (offset + frag_len < res->name_length) {
			ull_bredr_lmp_name_req(conn, offset + frag_len);
		} else {
			LOG_DBG("Remote name: %s", conn->remote_name);
		}
		break;
	}

	case LMP_ACCEPTED: {
		struct pdu_lmp_accepted *acc = (struct pdu_lmp_accepted *)pdu;
		LOG_DBG("LMP_accepted for opcode 0x%02x", acc->opcode);
		break;
	}

	case LMP_NOT_ACCEPTED: {
		struct pdu_lmp_not_accepted *rej = (struct pdu_lmp_not_accepted *)pdu;
		LOG_WRN("LMP_not_accepted for opcode 0x%02x, error=0x%02x",
			rej->opcode, rej->error_code);
		break;
	}

	case LMP_DETACH: {
		struct pdu_lmp_detach *det = (struct pdu_lmp_detach *)pdu;
		LOG_INF("LMP_detach received, reason=0x%02x", det->reason);
		ull_bredr_conn_cleanup(conn, det->reason);
		break;
	}

	case LMP_HOST_CONNECTION_REQ: {
		struct pdu_lmp_accepted acc;

		acc.hdr.tid = hdr->tid;
		acc.hdr.opcode = LMP_ACCEPTED;
		acc.opcode = LMP_HOST_CONNECTION_REQ;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));
		break;
	}

	case LMP_SETUP_COMPLETE: {
		struct pdu_lmp_accepted acc;

		acc.hdr.tid = hdr->tid;
		acc.hdr.opcode = LMP_ACCEPTED;
		acc.opcode = LMP_SETUP_COMPLETE;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));

		/* Connection setup complete */
		conn->lll.state = BREDR_CONN_STATE_CONNECTED;
		LOG_INF("Connection setup complete");
		break;
	}

	default:
		/* Handle extended opcodes */
		if (opcode >= LMP_ESCAPE_1 && opcode <= LMP_ESCAPE_4) {
			struct pdu_lmp_ext_header *ext_hdr =
				(struct pdu_lmp_ext_header *)pdu;
			LOG_DBG("Extended LMP opcode: escape=0x%02x, ext=0x%02x",
				ext_hdr->escape, ext_hdr->ext_opcode);

			/* Handle extended opcodes based on escape code */
			switch (ext_hdr->ext_opcode) {
			case LMP_EXT_ACCEPTED:
			case LMP_EXT_NOT_ACCEPTED:
				/* Extended accepted/not accepted */
				LOG_DBG("Extended LMP response received");
				break;

			case LMP_EXT_FEATURES_REQ:
			case LMP_EXT_FEATURES_RES:
				/* Extended features exchange */
				lmp_proc_features_rx(conn, pdu, len);
				break;

			case LMP_EXT_PACKET_TYPE_TABLE_REQ:
				/* Packet type table request */
				lmp_proc_packet_type_table_rx(conn, pdu, len);
				break;

			case LMP_EXT_ESCO_LINK_REQ:
			case LMP_EXT_REMOVE_ESCO_LINK_REQ:
				/* eSCO procedures */
				lmp_proc_esco_rx(conn, pdu, len);
				break;

			case LMP_EXT_IO_CAPABILITY_REQ:
			case LMP_EXT_IO_CAPABILITY_RES:
			case LMP_EXT_NUMERIC_COMPARISON_FAILED:
			case LMP_EXT_PASSKEY_FAILED:
			case LMP_EXT_OOB_FAILED:
			case LMP_EXT_KEYPRESS_NOTIFICATION:
				/* SSP procedures */
				lmp_proc_ssp_rx(conn, pdu, len);
				break;

			case LMP_EXT_PAUSE_ENCRYPTION_REQ:
			case LMP_EXT_RESUME_ENCRYPTION_REQ:
				/* EPR procedures */
				lmp_proc_encryption_rx(conn, pdu, len);
				break;

			case LMP_EXT_PING_REQ:
			case LMP_EXT_PING_RES:
				/* Ping */
				lmp_proc_ping_rx(conn, pdu, len);
				break;

			default:
				/* Unknown extended opcode */
				{
					struct pdu_lmp_ext_not_accepted rej;

					rej.hdr.tid = ext_hdr->tid;
					rej.hdr.escape = LMP_ESCAPE_4;
					rej.hdr.ext_opcode = LMP_EXT_NOT_ACCEPTED;
					rej.escape_opcode = ext_hdr->escape;
					rej.ext_opcode = ext_hdr->ext_opcode;
					rej.error_code = BT_HCI_ERR_UNKNOWN_LMP_PDU;

					ull_bredr_tx_enqueue(conn, &rej, sizeof(rej));
				}
				break;
			}
		} else {
			/* Unknown opcode - send LMP_not_accepted */
			struct pdu_lmp_not_accepted rej;

			rej.hdr.tid = hdr->tid;
			rej.hdr.opcode = LMP_NOT_ACCEPTED;
			rej.opcode = opcode;
			rej.error_code = BT_HCI_ERR_UNKNOWN_LMP_PDU;

			ull_bredr_tx_enqueue(conn, &rej, sizeof(rej));
		}
		break;
	}
}

/*
 * TX Data
 */
int ull_bredr_tx_enqueue(struct ull_bredr_conn *conn, void *pdu, uint16_t len)
{
	struct lmp_tx_node *tx_node;
	uint8_t link_id;

	/* Validate connection */
	if (!conn || conn->lll.handle == 0xFFFF) {
		LOG_ERR("Invalid connection");
		return -EINVAL;
	}

	/* Validate PDU length */
	if (len > 17) {
		LOG_ERR("LMP PDU too long: %u", len);
		return -EINVAL;
	}

	/* Get link_id from connection handle */
	link_id = conn->lll.handle;

	/* Acquire TX node */
	tx_node = lmp_tx_acquire();
	if (!tx_node) {
		LOG_ERR("No LMP TX node available");
		return -ENOMEM;
	}

	/* Fill TX node */
	tx_node->link_id = link_id;
	tx_node->len = (uint8_t)len;
	memcpy(tx_node->pdu, pdu, len);

	/* Enqueue to connection's LMP TX pending list */
	sys_slist_append(&conn->lmp_tx_pending, &tx_node->node);

	LOG_DBG("TX enqueue: handle=%u, len=%u, opcode=0x%02x",
		conn->lll.handle, len, ((uint8_t *)pdu)[0]);

	return 0;
}

/*
 * SCO/eSCO Management
 */
struct ull_bredr_sco *ull_bredr_sco_acquire(void)
{
	return mem_acquire(&sco_free);
}

void ull_bredr_sco_release(struct ull_bredr_sco *sco)
{
	mem_release(sco, &sco_free);
}

struct ull_bredr_sco *ull_bredr_sco_get(uint8_t idx)
{
	if (idx >= ULL_BREDR_SCO_MAX) {
		return NULL;
	}
	return &sco_pool[idx];
}

int ull_bredr_sco_start(struct ull_bredr_sco *sco, uint8_t t_sco)
{
	uint8_t sco_idx;
	uint32_t ticks_anchor;
	int err;

	sco_idx = mem_index_get(sco, sco_pool, sizeof(struct ull_bredr_sco));
	if (sco_idx >= ULL_BREDR_SCO_MAX) {
		return -EINVAL;
	}

	/* Initialize LLL header */
	lll_hdr_init(&sco->lll.hdr, &sco->ull);

	/* Start SCO ticker */
	ticks_anchor = ticker_ticks_now_get();
	err = ticker_bredr_sco_start(sco_idx, sco, ticks_anchor, t_sco);
	if (err) {
		LOG_ERR("Failed to start SCO ticker: %d", err);
		return err;
	}

	LOG_DBG("SCO started: idx=%u, t_sco=%u", sco_idx, t_sco);
	return 0;
}

int ull_bredr_sco_stop(struct ull_bredr_sco *sco)
{
	uint8_t sco_idx;
	int err;

	sco_idx = mem_index_get(sco, sco_pool, sizeof(struct ull_bredr_sco));
	if (sco_idx >= ULL_BREDR_SCO_MAX) {
		return -EINVAL;
	}

	/* Stop SCO ticker */
	err = ticker_bredr_sco_stop(sco_idx);
	if (err && err != -EALREADY) {
		LOG_WRN("Failed to stop SCO ticker: %d", err);
	}

	LOG_DBG("SCO stopped: idx=%u", sco_idx);
	return 0;
}

struct ull_bredr_esco *ull_bredr_esco_acquire(void)
{
	return mem_acquire(&esco_free);
}

void ull_bredr_esco_release(struct ull_bredr_esco *esco)
{
	mem_release(esco, &esco_free);
}

struct ull_bredr_esco *ull_bredr_esco_get(uint8_t idx)
{
	if (idx >= ULL_BREDR_ESCO_MAX) {
		return NULL;
	}
	return &esco_pool[idx];
}

int ull_bredr_esco_start(struct ull_bredr_esco *esco, uint8_t t_esco)
{
	uint8_t esco_idx;
	uint32_t ticks_anchor;
	int err;

	esco_idx = mem_index_get(esco, esco_pool, sizeof(struct ull_bredr_esco));
	if (esco_idx >= ULL_BREDR_ESCO_MAX) {
		return -EINVAL;
	}

	/* Initialize LLL header */
	lll_hdr_init(&esco->lll.hdr, &esco->ull);

	/* Start eSCO ticker */
	ticks_anchor = ticker_ticks_now_get();
	err = ticker_bredr_esco_start(esco_idx, esco, ticks_anchor, t_esco);
	if (err) {
		LOG_ERR("Failed to start eSCO ticker: %d", err);
		return err;
	}

	LOG_DBG("eSCO started: idx=%u, t_esco=%u", esco_idx, t_esco);
	return 0;
}

int ull_bredr_esco_stop(struct ull_bredr_esco *esco)
{
	uint8_t esco_idx;
	int err;

	esco_idx = mem_index_get(esco, esco_pool, sizeof(struct ull_bredr_esco));
	if (esco_idx >= ULL_BREDR_ESCO_MAX) {
		return -EINVAL;
	}

	/* Stop eSCO ticker */
	err = ticker_bredr_esco_stop(esco_idx);
	if (err && err != -EALREADY) {
		LOG_WRN("Failed to stop eSCO ticker: %d", err);
	}

	LOG_DBG("eSCO stopped: idx=%u", esco_idx);
	return 0;
}

/*
 * LT Address Management
 * Bluetooth Core Spec Vol 2, Part B - Logical Transport Address
 */
uint8_t ull_bredr_lt_addr_alloc(void)
{
	uint8_t i;

	for (i = LT_ADDR_MIN; i <= LT_ADDR_MAX; i++) {
		if (!(lm_env.lt_addr_bitmap & BIT(i))) {
			break;
		}
	}

	if (i > LT_ADDR_MAX) {
		i = 0;
	} else {
		lm_env.lt_addr_bitmap |= BIT(i);
	}

	return i;
}

void ull_bredr_lt_addr_free(uint8_t lt_addr)
{
	if ((lt_addr >= LT_ADDR_MIN) && (lt_addr <= LT_ADDR_MAX)) {
		lm_env.lt_addr_bitmap &= ~BIT(lt_addr);
	} else {
		LOG_WRN("Invalid LT address: %u", lt_addr);
	}
}

bool ull_bredr_lt_addr_reserve(uint8_t lt_addr)
{
	bool reserved = false;

	if ((lt_addr >= LT_ADDR_MIN) && (lt_addr <= LT_ADDR_MAX) &&
	    !(lm_env.lt_addr_bitmap & BIT(lt_addr))) {
		lm_env.lt_addr_bitmap |= BIT(lt_addr);
		reserved = true;
	}

	return reserved;
}

/*
 * Role Switch
 * Bluetooth Core Spec Vol 2, Part C - Role switch procedure
 */
bool ull_bredr_role_switch_start(uint8_t link_id, uint8_t *lt_addr)
{
	bool switch_allowed = true;

	if (link_id >= ULL_BREDR_MAX_CONN) {
		return false;
	}

	/* Slave->Master: allocate LT address */
	if (lm_env.con_info[link_id].role == BREDR_ROLE_PERIPHERAL) {
		lm_env.con_info[link_id].lt_addr = ull_bredr_lt_addr_alloc();

		if (lm_env.con_info[link_id].lt_addr == 0x00) {
			switch_allowed = false;
		} else {
			*lt_addr = lm_env.con_info[link_id].lt_addr;
		}
	}

	if (switch_allowed) {
		lm_env.con_info[link_id].state = BREDR_LINK_SWITCH;
	}

	return switch_allowed;
}

void ull_bredr_role_switch_finished(uint8_t link_id, bool success)
{
	if (link_id >= ULL_BREDR_MAX_CONN) {
		return;
	}

	if (success) {
		if (lm_env.con_info[link_id].role == BREDR_ROLE_CENTRAL) {
			/* Free LT Address */
			ull_bredr_lt_addr_free(lm_env.con_info[link_id].lt_addr);
			lm_env.con_info[link_id].lt_addr = 0x00;
		} else {
			if (!lm_env.afh.active) {
				lm_env.afh.active = 1;
				/* Start AFH timer using kernel work queue */
				ull_bredr_afh_activate_timer();
			}
		}

		/* Invert connection role */
		lm_env.con_info[link_id].role = !lm_env.con_info[link_id].role;
	} else if (lm_env.con_info[link_id].role == BREDR_ROLE_PERIPHERAL) {
		/* Free LT Address */
		ull_bredr_lt_addr_free(lm_env.con_info[link_id].lt_addr);
		lm_env.con_info[link_id].lt_addr = 0x00;
	}

	/* Clear the connection state to connected */
	lm_env.con_info[link_id].state = BREDR_LINK_CONNECTED;
}

/*
 * ACL Connection Count
 */
uint8_t ull_bredr_get_nb_acl(uint8_t acl_flag)
{
	uint8_t slave_nb = 0, master_nb = 0, link_nb = 0;
	uint8_t link_id;

	for (link_id = 0; link_id < ULL_BREDR_MAX_CONN; link_id++) {
		if (lm_env.con_info[link_id].state >= BREDR_LINK_CONNECTED) {
			if (lm_env.con_info[link_id].role == BREDR_ROLE_PERIPHERAL) {
				slave_nb++;
			} else {
				master_nb++;
			}
		}
	}

	if (acl_flag & 0x01) {  /* MASTER_FLAG */
		link_nb = master_nb;
	}

	if (acl_flag & 0x02) {  /* SLAVE_FLAG */
		link_nb += slave_nb;
	}

	return link_nb;
}

/*
 * ACL Connection Check
 */
bool ull_bredr_is_acl_con(uint8_t link_id)
{
	if (link_id >= ULL_BREDR_MAX_CONN) {
		return false;
	}
	return (lm_env.con_info[link_id].state == BREDR_LINK_CONNECTED);
}

bool ull_bredr_is_acl_con_role(uint8_t link_id, uint8_t role)
{
	if (link_id >= ULL_BREDR_MAX_CONN) {
		return false;
	}
	return ((lm_env.con_info[link_id].state == BREDR_LINK_CONNECTED) &&
		(lm_env.con_info[link_id].role == role));
}

/*
 * ACL Disconnect
 */
void ull_bredr_acl_disc(uint8_t link_id)
{
	if (link_id >= ULL_BREDR_MAX_CONN) {
		return;
	}

	/* Free link identifier */
	lm_env.con_info[link_id].state = BREDR_LINK_FREE;

	if (lm_env.con_info[link_id].role == BREDR_ROLE_CENTRAL) {
		/* Free LT address */
		ull_bredr_lt_addr_free(lm_env.con_info[link_id].lt_addr);
	}

	/* Check if page scan is enabled */
	if (lm_env.hci.scan_en & 0x02) {  /* PAGE_SCAN_ENABLE */
		/* Try to start page scan */
		lm_env.con_info[link_id].state = BREDR_LINK_PAGE_SCAN;
	}
}

/*
 * AFH Management
 * Bluetooth Core Spec Vol 2, Part B - Adaptive Frequency Hopping
 */
void ull_bredr_afh_set(struct ull_bredr_conn *conn, bool start)
{
	if (!conn) {
		return;
	}

	/* Get the new master channel map */
	uint8_t *ch_map = ull_bredr_afh_master_ch_map_get();

	/* Check if the map is different from the preceding one */
	if (start || memcmp(&conn->afh.ch_map[0], ch_map, 10)) {
		/* Compute the Hopping Scheme Switch Instant (HSSI) */
		uint32_t instant;

		/* Use 6 polling intervals */
		instant = 6 * conn->link.poll_interval;

		/* Switch instant must be at least 96 slots in the future */
		if (instant < 96 + 4) {
			instant = 96 + 4;
		}

		/* Store new map */
		memcpy(&conn->afh.ch_map[0], ch_map, 10);

		/* Send LMP_SetAFH */
		uint8_t link_id = ull_bredr_conn_handle_get(conn);
		ull_bredr_send_pdu_set_afh(link_id, instant, AFH_ENABLED,
					  conn->link.role);
		conn->afh.en = true;

		LOG_DBG("AFH set: instant=%u", instant);
	}
}

void ull_bredr_afh_peer_ch_class_set(uint8_t link_id, uint8_t *ch_class)
{
	if (link_id < ULL_BREDR_MAX_CONN) {
		memcpy(&lm_env.afh.peer_ch_class[link_id][0], ch_class, 10);
	}
}

uint8_t *ull_bredr_afh_host_ch_class_get(void)
{
	return &lm_env.afh.host_ch_class[0];
}

uint8_t *ull_bredr_afh_master_ch_map_get(void)
{
	return &lm_env.afh.master_ch_map[0];
}

void ull_bredr_afh_activate_timer(void)
{
	if (!lm_env.afh.active) {
		lm_env.afh.active = 1;
	}

	/* AFH channel assessment is typically done periodically.
	 * The timer triggers channel map updates based on channel quality.
	 * For now, we just mark AFH as active - actual timer implementation
	 * would use k_work_delayable to periodically assess channel quality
	 * and update the AFH map.
	 */
	LOG_DBG("AFH timer activated");
}

/*
 * SAM Management
 * Bluetooth Core Spec Vol 2, Part B - Slot Availability Mask
 */
uint16_t ull_bredr_sam_intv_get(uint8_t link_id)
{
	if (link_id >= ULL_BREDR_MAX_CONN) {
		return 0;
	}

	struct ull_bredr_conn *conn = &conn_pool[link_id];

	/* If both peer & local SAM maps active, return the larger t_sam */
	uint16_t t_sam = MAX(conn->sam_info.loc_t_sam_av,
			     conn->sam_info.rem_t_sam_av);

	return t_sam;
}

void ull_bredr_sam_disable(uint8_t link_id)
{
	if (link_id >= ULL_BREDR_MAX_CONN) {
		return;
	}

	struct ull_bredr_conn *conn = &conn_pool[link_id];

	if ((conn->sam_info.loc_idx != SAM_DISABLED) ||
	    (conn->sam_info.rem_idx != SAM_DISABLED)) {
		/* Send status change event */
		LOG_DBG("SAM disabled for link %u", link_id);
	}
}

/*
 * Feature Helpers
 * Bluetooth Core Spec Vol 2, Part C - Feature exchange
 */
void ull_bredr_read_features(uint8_t page_nb, uint8_t *page_nb_max,
			     uint8_t *feats)
{
	if (page_nb_max != NULL) {
		*page_nb_max = 2;  /* FEATURE_PAGE_MAX - 1 */
	}

	if (page_nb < 3) {
		if (page_nb == 0) {
			memcpy(feats, local_features, 8);
		} else {
			memcpy(feats, local_ext_features, 8);
		}

		/* Add variable bits for page 1 */
		if (page_nb == 1) {
			feats[0] |= ((lm_env.hci.sp_mode & 0x01) << 0);
			feats[0] |= ((lm_env.hci.le_supported_host & 0x01) << 1);
			feats[0] |= ((lm_env.hci.simultaneous_le_host & 0x01) << 2);
			feats[0] |= ((lm_env.hci.sec_con_host_supp & 0x01) << 3);
		}
	}
}

bool ull_bredr_get_feature(uint8_t *features, uint8_t bit_pos)
{
	return (features[bit_pos / 8] & BIT(bit_pos % 8)) != 0;
}

/*
 * Authentication/Encryption Helpers
 */
bool ull_bredr_get_auth_en(void)
{
	return (lm_env.hci.auth_en != 0);
}

bool ull_bredr_get_sp_en(void)
{
	return (lm_env.hci.sp_mode != 0);
}

bool ull_bredr_get_sec_con_host_supp(void)
{
	return (lm_env.hci.sec_con_host_supp != 0);
}

uint8_t ull_bredr_get_pin_type(void)
{
	return lm_env.hci.pin_type;
}

uint16_t ull_bredr_get_connection_accept_timeout(void)
{
	return lm_env.hci.con_accept_to;
}

void ull_bredr_get_local_name_seg(uint8_t *name_seg, uint8_t name_offset,
				  uint8_t *name_len)
{
	uint8_t index;

	*name_len = local_name_len;

	/* Reset the name segment */
	memset(name_seg, 0x00, 14);

	/* Fill data from stored local name */
	for (index = 0;
	     (index < 14) && ((index + name_offset) < 248) &&
	     ((index + name_offset) < local_name_len);
	     index++) {
		name_seg[index] = local_name[index + name_offset];
	}
}

uint8_t ull_bredr_get_loopback_mode(void)
{
	return lm_env.hci.loopback_mode;
}

bool ull_bredr_sp_debug_mode_get(void)
{
	return (lm_env.hci.sp_debug_mode != 0);
}

bool ull_bredr_dut_mode_en_get(void)
{
	return lm_env.hci.dut_mode_en;
}

/*
 * LM Environment Access
 */
struct ull_bredr_lm_env *ull_bredr_lm_env_get(void)
{
	return &lm_env;
}

/*
 * LMP PDU Send Functions
 * Bluetooth Core Spec Vol 2, Part C - LMP PDU transmission
 */

/**
 * @brief Acquire an LMP TX node from the pool
 */
static struct lmp_tx_node *lmp_tx_acquire(void)
{
	return mem_acquire(&lmp_tx_free);
}

/**
 * @brief Release an LMP TX node back to the pool
 */
static void lmp_tx_node_release(struct lmp_tx_node *node)
{
	mem_release(node, &lmp_tx_free);
}

/**
 * @brief Send an LMP PDU on a connection
 *
 * This function allocates a TX node, copies the PDU data, and enqueues
 * it to the connection's LMP TX pending list for transmission by LLL.
 *
 * @param link_id Connection link ID
 * @param param   Pointer to LMP PDU data
 * @param len     Length of LMP PDU
 */
void ull_bredr_send_lmp(uint8_t link_id, void *param, uint8_t len)
{
	struct ull_bredr_conn *conn;
	struct lmp_tx_node *tx_node;

	/* Validate link_id */
	if (link_id >= ULL_BREDR_MAX_CONN) {
		LOG_ERR("Invalid link_id: %u", link_id);
		return;
	}

	/* Get connection */
	conn = &conn_pool[link_id];
	if (conn->lll.handle == 0xFFFF) {
		LOG_ERR("Connection not active: link_id=%u", link_id);
		return;
	}

	/* Validate PDU length */
	if (len > 17) {
		LOG_ERR("LMP PDU too long: %u", len);
		return;
	}

	/* Acquire TX node */
	tx_node = lmp_tx_acquire();
	if (!tx_node) {
		LOG_ERR("No LMP TX node available");
		return;
	}

	/* Fill TX node */
	tx_node->link_id = link_id;
	tx_node->len = len;
	memcpy(tx_node->pdu, param, len);

	/* Enqueue to connection's LMP TX pending list */
	sys_slist_append(&conn->lmp_tx_pending, &tx_node->node);

	LOG_DBG("LMP TX enqueued: link_id=%u, len=%u, opcode=0x%02x",
		link_id, len, ((uint8_t *)param)[0]);
}

/**
 * @brief Dequeue an LMP PDU for transmission
 *
 * Called by LLL to get the next LMP PDU to transmit.
 *
 * @param conn Connection context
 * @return Pointer to LMP TX node, or NULL if queue is empty
 */
struct lmp_tx_node *ull_bredr_lmp_tx_dequeue(struct ull_bredr_conn *conn)
{
	sys_snode_t *node;

	node = sys_slist_get(&conn->lmp_tx_pending);
	if (!node) {
		return NULL;
	}

	return CONTAINER_OF(node, struct lmp_tx_node, node);
}

/**
 * @brief Release an LMP TX node after transmission
 *
 * Called by LLL after the LMP PDU has been transmitted.
 *
 * @param tx_node TX node to release
 */
void ull_bredr_lmp_tx_release(struct lmp_tx_node *tx_node)
{
	lmp_tx_node_release(tx_node);
}

void ull_bredr_send_pdu_acc(uint8_t idx, uint8_t opcode, uint8_t tr_id)
{
	struct pdu_lmp_accepted pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_ACCEPTED;
	pdu.opcode = opcode;

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_acc_ext4(uint8_t idx, uint8_t opcode, uint8_t tr_id)
{
	struct pdu_lmp_ext_accepted pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.escape = LMP_ESCAPE_4;
	pdu.hdr.ext_opcode = LMP_EXT_ACCEPTED;
	pdu.escape_opcode = LMP_ESCAPE_4;
	pdu.ext_opcode = opcode;

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_not_acc(uint8_t idx, uint8_t opcode, uint8_t reason,
				uint8_t tr_id)
{
	struct pdu_lmp_not_accepted pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_NOT_ACCEPTED;
	pdu.opcode = opcode;
	pdu.error_code = reason;

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_not_acc_ext4(uint8_t idx, uint8_t opcode, uint8_t reason,
				     uint8_t tr_id)
{
	struct pdu_lmp_ext_not_accepted pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.escape = LMP_ESCAPE_4;
	pdu.hdr.ext_opcode = LMP_EXT_NOT_ACCEPTED;
	pdu.escape_opcode = LMP_ESCAPE_4;
	pdu.ext_opcode = opcode;
	pdu.error_code = reason;

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_set_afh(uint8_t idx, uint32_t instant, uint8_t mode,
				uint8_t tr_id)
{
	struct pdu_lmp_set_afh pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_SET_AFH;
	pdu.afh_instant = sys_cpu_to_le32(instant);
	pdu.afh_mode = mode;
	memcpy(pdu.afh_channel_map, &lm_env.afh.master_ch_map[0], 10);

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_au_rand(uint8_t idx, uint8_t *random, uint8_t tr_id)
{
	struct pdu_lmp_au_rand pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_AU_RAND;
	memcpy(pdu.random_number, random, 16);

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_sres(uint8_t idx, uint8_t *sres, uint8_t tr_id)
{
	struct pdu_lmp_sres pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_SRES;
	memcpy(pdu.auth_res, sres, 4);

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_vers_req(uint8_t idx, uint8_t role)
{
	struct pdu_lmp_version pdu;

	pdu.hdr.tid = role;
	pdu.hdr.opcode = LMP_VERSION_REQ;
	pdu.vers_nr = 0x0C;  /* Bluetooth 6.0 */
	pdu.comp_id = sys_cpu_to_le16(0xFFFF);
	pdu.sub_vers_nr = sys_cpu_to_le16(0x0001);

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_feats_res(uint8_t idx, uint8_t tr_id)
{
	struct pdu_lmp_features pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_FEATURES_RES;
	ull_bredr_read_features(0, NULL, pdu.features);

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_setup_cmp(uint8_t idx, uint8_t tr_id)
{
	struct pdu_lmp_setup_complete pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_SETUP_COMPLETE;

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_lsto(uint8_t idx, uint16_t timeout, uint8_t role)
{
	struct pdu_lmp_supervision_timeout pdu;

	pdu.hdr.tid = role;
	pdu.hdr.opcode = LMP_SUPERVISION_TIMEOUT;
	pdu.supervision_timeout = sys_cpu_to_le16(timeout);

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_max_slot(uint8_t idx, uint8_t max_slot, uint8_t tr_id)
{
	struct {
		struct pdu_lmp_header hdr;
		uint8_t max_slots;
	} __packed pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_MAX_SLOT;
	pdu.max_slots = max_slot;

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_auto_rate(uint8_t idx, uint8_t tr_id)
{
	struct pdu_lmp_header pdu;

	pdu.tid = tr_id;
	pdu.opcode = LMP_AUTO_RATE;

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

/*
 * Utility Functions
 */
void ull_bredr_util_set_loc_trans_coll(uint8_t link_id, uint8_t opcode,
				       uint8_t opcode_ext, uint8_t mode)
{
	if (link_id >= ULL_BREDR_MAX_CONN) {
		return;
	}

	struct ull_bredr_conn *conn = &conn_pool[link_id];
	conn->local_trans.opcode = opcode;
	conn->local_trans.opcode_ext = opcode_ext;
	conn->local_trans.in_use = mode;
}


void ull_bredr_send_pdu_feats_ext_req(uint8_t idx, uint8_t page, uint8_t tr_id)
{
	struct {
		struct pdu_lmp_ext_header hdr;
		uint8_t page;
		uint8_t max_page;
		uint8_t ext_feats[8];
	} __packed pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.escape = LMP_ESCAPE_4;
	pdu.hdr.ext_opcode = LMP_EXT_FEATURES_REQ;
	pdu.page = page;
	ull_bredr_read_features(page, &pdu.max_page, pdu.ext_feats);

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_max_slot_req(uint8_t idx, uint8_t max_slot, uint8_t tr_id)
{
	struct {
		struct pdu_lmp_header hdr;
		uint8_t max_slots;
	} __packed pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_MAX_SLOT_REQ;
	pdu.max_slots = max_slot;

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_qos_req(uint8_t idx, uint8_t nb_bcst, uint16_t poll_int,
				uint8_t tr_id)
{
	struct pdu_lmp_qos_req pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_QUALITY_OF_SERVICE_REQ;
	pdu.poll_interval = sys_cpu_to_le16(poll_int);
	pdu.nbc = nb_bcst;

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_enc_key_sz_req(uint8_t idx, uint8_t key_size, uint8_t tr_id)
{
	struct pdu_lmp_encryption_key_size_req pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_ENCRYPTION_KEY_SIZE_REQ;
	pdu.key_size = key_size;

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_io_cap_res(uint8_t idx)
{
	struct ull_bredr_conn *conn = ull_bredr_conn_get(idx);
	if (!conn) {
		return;
	}

	struct pdu_lmp_io_capability pdu;

	pdu.hdr.tid = conn->sp.sp_tid;
	pdu.hdr.escape = LMP_ESCAPE_4;
	pdu.hdr.ext_opcode = LMP_EXT_IO_CAPABILITY_RES;
	pdu.io_capability = conn->sp.io_cap_loc[0];
	pdu.oob_data_present = conn->sp.io_cap_loc[1];
	pdu.auth_requirements = conn->sp.io_cap_loc[2];

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_sp_nb(uint8_t idx, uint8_t *data, uint8_t tr_id)
{
	struct {
		struct pdu_lmp_header hdr;
		uint8_t nonce[16];
	} __packed pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_SP_NUMBER;
	memcpy(pdu.nonce, data, 16);

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_sp_cfm(uint8_t idx, uint8_t *data, uint8_t tr_id)
{
	struct {
		struct pdu_lmp_header hdr;
		uint8_t commitment[16];
	} __packed pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_SP_CONFIRM;
	memcpy(pdu.commitment, data, 16);

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_dhkey_chk(uint8_t idx, uint8_t *dhkey, uint8_t tr_id)
{
	struct {
		struct pdu_lmp_header hdr;
		uint8_t cfm_val[16];
	} __packed pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_DHKEY_CHECK;
	memcpy(pdu.cfm_val, dhkey, 16);

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_paus_enc_req(uint8_t idx, uint8_t tr_id)
{
	struct pdu_lmp_ext_header pdu;

	pdu.tid = tr_id;
	pdu.escape = LMP_ESCAPE_4;
	pdu.ext_opcode = LMP_EXT_PAUSE_ENCRYPTION_REQ;

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_resu_enc_req(uint8_t idx, uint8_t tr_id)
{
	struct pdu_lmp_ext_header pdu;

	pdu.tid = tr_id;
	pdu.escape = LMP_ESCAPE_4;
	pdu.ext_opcode = LMP_EXT_RESUME_ENCRYPTION_REQ;

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_ptt_req(uint8_t idx, uint8_t ptt, uint8_t tr_id)
{
	struct {
		struct pdu_lmp_ext_header hdr;
		uint8_t pkt_type_tbl;
	} __packed pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.escape = LMP_ESCAPE_4;
	pdu.hdr.ext_opcode = LMP_EXT_PACKET_TYPE_TABLE_REQ;
	pdu.pkt_type_tbl = ptt;

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_in_rand(uint8_t idx, uint8_t *random, uint8_t tr_id)
{
	struct pdu_lmp_au_rand pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_IN_RAND;
	memcpy(pdu.random_number, random, 16);

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

void ull_bredr_send_pdu_comb_key(uint8_t idx, uint8_t *random, uint8_t tr_id)
{
	struct {
		struct pdu_lmp_header hdr;
		uint8_t random[16];
	} __packed pdu;

	pdu.hdr.tid = tr_id;
	pdu.hdr.opcode = LMP_COMB_KEY;
	memcpy(pdu.random, random, 16);

	ull_bredr_send_lmp(idx, &pdu, sizeof(pdu));
}

/*
 * LMP Procedure Functions
 * Bluetooth Core Spec Vol 2, Part C - LMP PDU transmission
 */

/* Authentication */
int ull_bredr_lmp_au_rand(struct ull_bredr_conn *conn, uint8_t *random)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	ull_bredr_send_pdu_au_rand(link_id, random, conn->link.role);
	return 0;
}

int ull_bredr_lmp_sres(struct ull_bredr_conn *conn, uint8_t *sres)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	ull_bredr_send_pdu_sres(link_id, sres, conn->link.role);
	return 0;
}

int ull_bredr_lmp_in_rand(struct ull_bredr_conn *conn, uint8_t *random)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	ull_bredr_send_pdu_in_rand(link_id, random, conn->link.role);
	return 0;
}

int ull_bredr_lmp_comb_key(struct ull_bredr_conn *conn, uint8_t *random)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	ull_bredr_send_pdu_comb_key(link_id, random, conn->link.role);
	return 0;
}

/* Encryption */
int ull_bredr_lmp_encryption_mode_req(struct ull_bredr_conn *conn, uint8_t mode)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_encryption_mode_req pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.opcode = LMP_ENCRYPTION_MODE_REQ;
	pdu.encryption_mode = mode;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

int ull_bredr_lmp_encryption_key_size_req(struct ull_bredr_conn *conn,
					  uint8_t key_size)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	ull_bredr_send_pdu_enc_key_sz_req(link_id, key_size, conn->link.role);
	return 0;
}

int ull_bredr_lmp_start_encryption_req(struct ull_bredr_conn *conn,
				       uint8_t *random)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_start_encryption_req pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.opcode = LMP_START_ENCRYPTION_REQ;
	memcpy(pdu.random_number, random, 16);

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

int ull_bredr_lmp_stop_encryption_req(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_header pdu;

	pdu.tid = conn->link.role;
	pdu.opcode = LMP_STOP_ENCRYPTION_REQ;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

/* Secure Simple Pairing */
int ull_bredr_lmp_io_capability_req(struct ull_bredr_conn *conn,
				    uint8_t io_cap, uint8_t oob, uint8_t auth_req)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_io_capability pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.escape = LMP_ESCAPE_4;
	pdu.hdr.ext_opcode = LMP_EXT_IO_CAPABILITY_REQ;
	pdu.io_capability = io_cap;
	pdu.oob_data_present = oob;
	pdu.auth_requirements = auth_req;

	/* Store local IO capabilities */
	conn->sp.io_cap_loc[0] = io_cap;
	conn->sp.io_cap_loc[1] = oob;
	conn->sp.io_cap_loc[2] = auth_req;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

int ull_bredr_lmp_io_capability_res(struct ull_bredr_conn *conn,
				    uint8_t io_cap, uint8_t oob, uint8_t auth_req)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_io_capability pdu;

	pdu.hdr.tid = conn->sp.sp_tid;
	pdu.hdr.escape = LMP_ESCAPE_4;
	pdu.hdr.ext_opcode = LMP_EXT_IO_CAPABILITY_RES;
	pdu.io_capability = io_cap;
	pdu.oob_data_present = oob;
	pdu.auth_requirements = auth_req;

	/* Store local IO capabilities */
	conn->sp.io_cap_loc[0] = io_cap;
	conn->sp.io_cap_loc[1] = oob;
	conn->sp.io_cap_loc[2] = auth_req;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

int ull_bredr_lmp_sp_confirm(struct ull_bredr_conn *conn, uint8_t *value)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	ull_bredr_send_pdu_sp_cfm(link_id, value, conn->link.role);
	return 0;
}

int ull_bredr_lmp_sp_number(struct ull_bredr_conn *conn, uint8_t *nonce)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	ull_bredr_send_pdu_sp_nb(link_id, nonce, conn->link.role);
	return 0;
}

int ull_bredr_lmp_dhkey_check(struct ull_bredr_conn *conn, uint8_t *value)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	ull_bredr_send_pdu_dhkey_chk(link_id, value, conn->link.role);
	return 0;
}

/* Power Modes */
int ull_bredr_lmp_hold_req(struct ull_bredr_conn *conn, uint16_t hold_time,
			   uint32_t hold_instant)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_hold pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.opcode = LMP_HOLD_REQ;
	pdu.hold_time = sys_cpu_to_le16(hold_time);
	pdu.hold_instant = sys_cpu_to_le32(hold_instant);

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

int ull_bredr_lmp_sniff_req(struct ull_bredr_conn *conn, uint16_t d_sniff,
			    uint16_t t_sniff, uint16_t attempt, uint16_t timeout)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_sniff_req pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.opcode = LMP_SNIFF_REQ;
	pdu.timing_control_flags = 0;
	pdu.d_sniff = sys_cpu_to_le16(d_sniff);
	pdu.t_sniff = sys_cpu_to_le16(t_sniff);
	pdu.sniff_attempt = sys_cpu_to_le16(attempt);
	pdu.sniff_timeout = sys_cpu_to_le16(timeout);

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

int ull_bredr_lmp_unsniff_req(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_header pdu;

	pdu.tid = conn->link.role;
	pdu.opcode = LMP_UNSNIFF_REQ;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

int ull_bredr_lmp_park_req(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct {
		struct pdu_lmp_header hdr;
		uint8_t timing_control_flags;
		uint16_t db;
		uint16_t tb;
		uint8_t nb;
		uint8_t delta_b;
		uint8_t pm_addr;
		uint8_t ar_addr;
		uint8_t nb_sleep;
		uint8_t db_sleep;
		uint8_t d_access;
		uint8_t t_access;
		uint8_t n_acc_slots;
		uint8_t n_poll;
		uint8_t m_access;
		uint8_t access_scheme;
	} __packed pdu;

	memset(&pdu, 0, sizeof(pdu));
	pdu.hdr.tid = conn->link.role;
	pdu.hdr.opcode = LMP_PARK_REQ;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

int ull_bredr_lmp_unpark_req(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct {
		struct pdu_lmp_header hdr;
		uint8_t timing_control_flags;
		uint8_t db;
		uint8_t lt_addr;
		uint8_t pm_addr;
		uint8_t ar_addr;
		uint8_t nb_sleep;
		uint8_t db_sleep;
		uint8_t d_access;
		uint8_t t_access;
		uint8_t n_acc_slots;
		uint8_t n_poll;
		uint8_t m_access;
		uint8_t access_scheme;
	} __packed pdu;

	memset(&pdu, 0, sizeof(pdu));
	pdu.hdr.tid = conn->link.role;
	pdu.hdr.opcode = LMP_UNPARK_BD_ADDR_REQ;
	pdu.lt_addr = conn->lll.lt_addr;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

/* Role Switch */
int ull_bredr_lmp_switch_req(struct ull_bredr_conn *conn, uint32_t instant)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_switch_req pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.opcode = LMP_SWITCH_REQ;
	pdu.switch_instant = sys_cpu_to_le32(instant);

	conn->link.switch_instant = instant;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

/* AFH */
int ull_bredr_lmp_set_afh(struct ull_bredr_conn *conn, uint32_t instant,
			  uint8_t mode, uint8_t *channel_map)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_set_afh pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.opcode = LMP_SET_AFH;
	pdu.afh_instant = sys_cpu_to_le32(instant);
	pdu.afh_mode = mode;
	if (channel_map) {
		memcpy(pdu.afh_channel_map, channel_map, 10);
	} else {
		memcpy(pdu.afh_channel_map, conn->afh.ch_map, 10);
	}

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

/* QoS */
int ull_bredr_lmp_qos_req(struct ull_bredr_conn *conn, uint16_t poll_interval,
			  uint8_t nbc)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	ull_bredr_send_pdu_qos_req(link_id, nbc, poll_interval, conn->link.role);
	return 0;
}

/* SCO */
int ull_bredr_lmp_sco_link_req(struct ull_bredr_conn *conn, uint8_t sco_handle,
			       uint8_t d_sco, uint8_t t_sco, uint8_t packet,
			       uint8_t air_mode)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_sco_link_req pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.opcode = LMP_SCO_LINK_REQ;
	pdu.sco_handle = sco_handle;
	pdu.timing_control_flags = 0;
	pdu.d_sco = d_sco;
	pdu.t_sco = t_sco;
	pdu.sco_packet = packet;
	pdu.air_mode = air_mode;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

int ull_bredr_lmp_remove_sco_link_req(struct ull_bredr_conn *conn,
				      uint8_t sco_handle, uint8_t reason)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_remove_sco_link_req pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.opcode = LMP_REMOVE_SCO_LINK_REQ;
	pdu.sco_handle = sco_handle;
	pdu.reason = reason;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

/* eSCO */
int ull_bredr_lmp_esco_link_req(struct ull_bredr_conn *conn,
				struct pdu_lmp_esco_link_req *params)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	params->hdr.tid = conn->link.role;
	params->hdr.escape = LMP_ESCAPE_4;
	params->hdr.ext_opcode = LMP_EXT_ESCO_LINK_REQ;

	ull_bredr_send_lmp(link_id, params, sizeof(*params));
	return 0;
}

int ull_bredr_lmp_remove_esco_link_req(struct ull_bredr_conn *conn,
				       uint8_t esco_handle, uint8_t reason)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_remove_esco_link_req pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.escape = LMP_ESCAPE_4;
	pdu.hdr.ext_opcode = LMP_EXT_REMOVE_ESCO_LINK_REQ;
	pdu.esco_handle = esco_handle;
	pdu.reason = reason;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}


/*
 * AFH Management Functions
 * Bluetooth Core Spec Vol 2, Part B - Adaptive Frequency Hopping
 */
void ull_bredr_afh_start(struct ull_bredr_conn *conn)
{
	if (!conn) {
		return;
	}

	/* Enable AFH for this connection */
	conn->afh.en = true;

	/* Set the AFH channel map */
	ull_bredr_afh_set(conn, true);

	LOG_DBG("AFH started: handle=%u", conn->lll.handle);
}

void ull_bredr_afh_stop_reporting(struct ull_bredr_conn *conn)
{
	if (!conn) {
		return;
	}

	/* Disable AFH channel classification reporting */
	conn->afh.reporting_en = false;

	LOG_DBG("AFH reporting stopped: handle=%u", conn->lll.handle);
}

void ull_bredr_afh_restore_reporting(struct ull_bredr_conn *conn)
{
	if (!conn) {
		return;
	}

	/* Re-enable AFH channel classification reporting */
	conn->afh.reporting_en = true;

	LOG_DBG("AFH reporting restored: handle=%u", conn->lll.handle);
}

/*
 * Connection Sequence Functions
 * Bluetooth Core Spec Vol 2, Part C - Connection establishment
 */
bool ull_bredr_conn_seq_done(struct ull_bredr_conn *conn)
{
	if (!conn) {
		return false;
	}

	/* Check if all mandatory connection setup procedures are complete */

	/* Version exchange must be complete */
	if (!conn->info.recv_rem_ver_rec) {
		return false;
	}

	/* Features exchange must be complete (at least page 0) */
	if (!(conn->info.remote_feat_rec & BIT(0))) {
		return false;
	}

	/* Setup complete must have been exchanged */
	if (!conn->link.setup_comp_rx || !conn->link.setup_comp_tx) {
		return false;
	}

	return true;
}

void ull_bredr_conn_complete(struct ull_bredr_conn *conn)
{
	struct net_buf *buf;
	struct bt_hci_evt_conn_complete *evt;

	if (!conn) {
		return;
	}

	/* Mark connection as complete */
	conn->link.connection_complete_sent = true;

	/* Send HCI Connection Complete event */
	buf = bt_buf_get_evt(BT_HCI_EVT_CONN_COMPLETE, false, K_NO_WAIT);
	if (!buf) {
		LOG_ERR("No buffer for Connection Complete event");
		return;
	}

	evt = net_buf_add(buf, sizeof(*evt));
	evt->status = 0;  /* Success */
	evt->handle = sys_cpu_to_le16(conn->lll.handle);
	memcpy(evt->bdaddr.val, conn->lll.bd_addr, 6);
	evt->link_type = 0x01;  /* ACL */
	evt->encr_enabled = (conn->enc.enc_mode != ENC_DISABLED) ? 1 : 0;

	bt_recv(buf);

	LOG_INF("Connection complete: handle=%u, BD_ADDR=%02x:%02x:%02x:%02x:%02x:%02x",
		conn->lll.handle,
		conn->lll.bd_addr[5], conn->lll.bd_addr[4], conn->lll.bd_addr[3],
		conn->lll.bd_addr[2], conn->lll.bd_addr[1], conn->lll.bd_addr[0]);
}

void ull_bredr_conn_complete_evt_send(struct ull_bredr_conn *conn, uint8_t status)
{
	struct net_buf *buf;
	struct bt_hci_evt_conn_complete *evt;

	if (!conn) {
		return;
	}

	/* Send HCI Connection Complete event with specified status */
	buf = bt_buf_get_evt(BT_HCI_EVT_CONN_COMPLETE, false, K_NO_WAIT);
	if (!buf) {
		LOG_ERR("No buffer for Connection Complete event");
		return;
	}

	evt = net_buf_add(buf, sizeof(*evt));
	evt->status = status;
	evt->handle = sys_cpu_to_le16(conn->lll.handle);
	memcpy(evt->bdaddr.val, conn->lll.bd_addr, 6);
	evt->link_type = 0x01;  /* ACL */
	evt->encr_enabled = (conn->enc.enc_mode != ENC_DISABLED) ? 1 : 0;

	bt_recv(buf);

	LOG_DBG("Connection complete event sent: handle=%u, status=%u",
		conn->lll.handle, status);
}

/*
 * SAM Management Functions
 * Bluetooth Core Spec Vol 2, Part B - Slot Availability Mask
 */
uint16_t ull_bredr_sam_loc_offset_get(uint8_t link_id)
{
	if (link_id >= ULL_BREDR_MAX_CONN) {
		return 0;
	}

	struct ull_bredr_conn *conn = &conn_pool[link_id];

	/* Return local SAM offset */
	return conn->sam_info.loc_tx_av;
}

uint16_t ull_bredr_sam_rem_offset_get(uint8_t link_id)
{
	if (link_id >= ULL_BREDR_MAX_CONN) {
		return 0;
	}

	struct ull_bredr_conn *conn = &conn_pool[link_id];

	/* Return remote SAM offset */
	return conn->sam_info.rem_tx_av;
}

uint16_t ull_bredr_sam_slot_av_get(uint8_t link_id, uint16_t offset_min,
				   uint16_t offset_max)
{
	if (link_id >= ULL_BREDR_MAX_CONN) {
		return 0;
	}

	struct ull_bredr_conn *conn = &conn_pool[link_id];

	/* Check if SAM is active */
	if (conn->sam_info.loc_idx == SAM_DISABLED &&
	    conn->sam_info.rem_idx == SAM_DISABLED) {
		/* SAM not active - all slots available */
		return 0xFFFF;
	}

	/* Return available slots within the specified range */
	/* This is a simplified implementation */
	return (conn->sam_info.loc_tx_av | conn->sam_info.rem_tx_av);
}

/*
 * Key Management Functions
 * Bluetooth Core Spec Vol 2, Part H - Key management
 */
void ull_bredr_get_pub_key_192(uint8_t *public_key)
{
	memcpy(public_key, lm_env.pub_key_192, 48);
}

void ull_bredr_get_priv_key_192(uint8_t *private_key)
{
	memcpy(private_key, lm_env.priv_key_192, 24);
}

void ull_bredr_get_pub_key_256(uint8_t *public_key)
{
	memcpy(public_key, lm_env.pub_key_256, 64);
}

void ull_bredr_get_priv_key_256(uint8_t *private_key)
{
	memcpy(private_key, lm_env.priv_key_256, 32);
}

void ull_bredr_get_oob_local_data_192(uint8_t *r, uint8_t *c)
{
	memcpy(r, lm_env.oob_r, 16);
	memcpy(c, lm_env.oob_c, 16);
}

void ull_bredr_get_oob_local_data_256(uint8_t *r, uint8_t *c)
{
	/* For P-256, use the same OOB data */
	memcpy(r, lm_env.oob_r, 16);
	memcpy(c, lm_env.oob_c, 16);
}

/*
 * Ticker Callback
 * Called by ticker when BR/EDR event occurs
 */
void ull_bredr_ticker_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
			 uint32_t remainder, uint16_t lazy, uint8_t force,
			 void *param)
{
	/* This callback is invoked by the ticker when a BR/EDR event
	 * (connection, inquiry, page, etc.) is scheduled to occur.
	 *
	 * The actual event handling is done in the LLL layer through
	 * the prepare callbacks set up during ticker start.
	 */
	ARG_UNUSED(ticks_at_expire);
	ARG_UNUSED(ticks_drift);
	ARG_UNUSED(remainder);
	ARG_UNUSED(lazy);
	ARG_UNUSED(force);
	ARG_UNUSED(param);
}

/*
 * Event Done Handling
 * Called when a BR/EDR event completes
 */
void ull_bredr_done(struct node_rx_event_done *done)
{
	/* Handle event completion
	 * This is called from the ULL done processing path
	 * when a BR/EDR event (connection, inquiry, page) completes.
	 */
	if (!done) {
		return;
	}

	/* Process based on event type */
	/* The done->extra field contains event-specific data */
	LOG_DBG("BR/EDR event done");
}

/*
 * RX Handling
 * Called when BR/EDR data is received
 */
void ull_bredr_rx(memq_link_t *link, struct node_rx_pdu **rx)
{
	/* Handle received BR/EDR data
	 * This is called from the ULL RX processing path
	 * when BR/EDR data (ACL, LMP, SCO) is received.
	 */
	if (!link || !rx || !*rx) {
		return;
	}

	/* Process the received PDU */
	LOG_DBG("BR/EDR RX processing");
}
