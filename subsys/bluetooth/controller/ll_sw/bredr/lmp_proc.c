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
 * BR/EDR LMP Procedures Implementation
 * Bluetooth Core Spec Vol 2, Part C: Link Manager Protocol
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/buf.h>
#include <zephyr/sys/byteorder.h>

#include "util/util.h"
#include "util/mem.h"
#include "util/memq.h"

#include "pdu_bredr.h"
#include "lll_bredr.h"
#include "ull_bredr.h"
#include "lmp_proc.h"

/* For bt_recv */
extern int bt_recv(struct net_buf *buf);

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_ctlr_lmp_proc, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

/*
 * Helper Functions
 */
bool lmp_proc_get_feature(uint8_t *features, uint8_t bit_pos)
{
	return (features[bit_pos / 8] & BIT(bit_pos % 8)) != 0;
}

uint8_t lmp_proc_max_slot(uint16_t packet_type)
{
	if (packet_type & 0xC000) {  /* DM5, DH5 */
		return 5;
	} else if (packet_type & 0x0C00) {  /* DM3, DH3 */
		return 3;
	} else {
		return 1;
	}
}

void lmp_proc_suppress_acl_packet(uint16_t *packet_type, uint8_t max_slot)
{
	if (max_slot == 5) {
		*packet_type &= ~0xC000;  /* Remove DM5, DH5 */
	} else if (max_slot == 3) {
		*packet_type &= ~0xCC00;  /* Remove DM3, DH3, DM5, DH5 */
	}
}

/*
 * Connection Setup Procedure
 * Bluetooth Core Spec Vol 2, Part C - Connection establishment
 */
int lmp_proc_connection_setup(struct ull_bredr_conn *conn)
{
	int err;

	LOG_DBG("Connection setup: handle=%u", conn->lll.handle);

	/* Set initiator flag */
	conn->link.initiator = true;
	conn->link.host_connected = true;

	/* Start LMP timeout */
	lmp_proc_start_lmp_to(conn);

	/* Exchange version information */
	err = lmp_proc_version_exchange(conn);
	if (err) {
		return err;
	}

	/* Exchange features */
	err = lmp_proc_features_exchange(conn);
	if (err) {
		return err;
	}

	return 0;
}

int lmp_proc_connection_complete(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	/* Send LMP_setup_complete */
	ull_bredr_send_pdu_setup_cmp(link_id, conn->link.role);

	conn->link.setup_comp_tx = true;

	return 0;
}

/*
 * LMP Timeout Management
 */

/* LMP Response Timeout value in milliseconds (30 seconds per spec) */
#define LMP_RESPONSE_TIMEOUT_MS  30000

/* Timeout work handler */
static void lmp_timeout_handler(struct k_work *work);

/* Timeout work items for each connection */
static struct k_work_delayable lmp_timeout_work[ULL_BREDR_MAX_CONN];

/* Connection pointers for timeout handling */
static struct ull_bredr_conn *lmp_timeout_conn[ULL_BREDR_MAX_CONN];

static void lmp_timeout_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	int idx;

	/* Find which connection this timeout belongs to */
	for (idx = 0; idx < ULL_BREDR_MAX_CONN; idx++) {
		if (&lmp_timeout_work[idx] == dwork) {
			break;
		}
	}

	if (idx >= ULL_BREDR_MAX_CONN) {
		LOG_ERR("LMP timeout: invalid work item");
		return;
	}

	struct ull_bredr_conn *conn = lmp_timeout_conn[idx];
	if (!conn || conn->lll.handle == 0xFFFF) {
		LOG_WRN("LMP timeout: connection no longer valid");
		return;
	}

	LOG_WRN("LMP response timeout: handle=%u", conn->lll.handle);

	/* Handle timeout based on current state */
	switch (conn->lc_state) {
	case BREDR_LC_WAIT_VERSION:
	case BREDR_LC_WAIT_VERSION_CENTRAL:
	case BREDR_LC_WAIT_FEATURES:
	case BREDR_LC_WAIT_EXT_FEATURES:
	case BREDR_LC_WAIT_NAME:
		/* Non-critical procedures - just log and continue */
		conn->lc_state = BREDR_LC_CONNECTED;
		break;

	case BREDR_LC_WAIT_AUTH_SRES:
	case BREDR_LC_WAIT_AU_RAND_RSP:
	case BREDR_LC_WAIT_SRES_RSP:
		/* Authentication timeout - fail authentication */
		lmp_proc_auth_cmp(conn, BT_HCI_ERR_CONN_TIMEOUT);
		break;

	case BREDR_LC_WAIT_ENC_MODE_CFM:
	case BREDR_LC_WAIT_ENC_SIZE_CFM:
	case BREDR_LC_WAIT_ENC_START_CFM:
	case BREDR_LC_WAIT_ENC_STOP_CFM:
		/* Encryption timeout - fail encryption */
		lmp_proc_enc_cmp(conn, BT_HCI_ERR_CONN_TIMEOUT);
		break;

	case BREDR_LC_WAIT_SWITCH_CFM:
	case BREDR_LC_WAIT_SWITCH_CMP:
		/* Role switch timeout */
		lmp_proc_switch_cmp(conn, BT_HCI_ERR_CONN_TIMEOUT);
		break;

	default:
		/* For other states, disconnect */
		lmp_proc_detach(conn, BT_HCI_ERR_CONN_TIMEOUT);
		break;
	}
}

void lmp_proc_start_lmp_to(struct ull_bredr_conn *conn)
{
	uint8_t idx = ull_bredr_conn_handle_get(conn);

	if (idx >= ULL_BREDR_MAX_CONN) {
		LOG_ERR("Invalid connection for LMP timeout");
		return;
	}

	/* Initialize work item if needed */
	if (!k_work_delayable_is_pending(&lmp_timeout_work[idx])) {
		k_work_init_delayable(&lmp_timeout_work[idx], lmp_timeout_handler);
	}

	/* Store connection pointer */
	lmp_timeout_conn[idx] = conn;

	/* Start timeout */
	k_work_schedule(&lmp_timeout_work[idx], K_MSEC(LMP_RESPONSE_TIMEOUT_MS));

	LOG_DBG("LMP timeout started: handle=%u", conn->lll.handle);
}

void lmp_proc_stop_lmp_to(struct ull_bredr_conn *conn)
{
	uint8_t idx = ull_bredr_conn_handle_get(conn);

	if (idx >= ULL_BREDR_MAX_CONN) {
		return;
	}

	/* Cancel timeout */
	k_work_cancel_delayable(&lmp_timeout_work[idx]);
	lmp_timeout_conn[idx] = NULL;

	LOG_DBG("LMP timeout stopped: handle=%u", conn->lll.handle);
}

/*
 * Version Exchange
 * Bluetooth Core Spec Vol 2, Part C - Version exchange procedure
 */
int lmp_proc_version_exchange(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	conn->req.loc_vers_req = true;
	ull_bredr_send_pdu_vers_req(link_id, conn->link.role);

	return 0;
}

void lmp_proc_version_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	if (hdr->opcode == LMP_VERSION_REQ) {
		struct pdu_lmp_version *req = (struct pdu_lmp_version *)pdu;

		conn->info.remote_vers = req->vers_nr;
		conn->info.remote_comp_id = sys_le16_to_cpu(req->comp_id);
		conn->info.remote_subvers = sys_le16_to_cpu(req->sub_vers_nr);
		conn->info.recv_rem_ver_rec = true;

		LOG_DBG("Remote version: %u, company=0x%04x",
			conn->info.remote_vers, conn->info.remote_comp_id);
	}
}

/*
 * Features Exchange
 * Bluetooth Core Spec Vol 2, Part C - Features exchange procedure
 */
int lmp_proc_features_exchange(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_features pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.opcode = LMP_FEATURES_REQ;
	ull_bredr_read_features(0, NULL, pdu.features);

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));

	return 0;
}

int lmp_proc_ext_features_exchange(struct ull_bredr_conn *conn, uint8_t page)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	ull_bredr_send_pdu_feats_ext_req(link_id, page, conn->link.role);

	return 0;
}

void lmp_proc_features_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	if (hdr->opcode == LMP_FEATURES_REQ || hdr->opcode == LMP_FEATURES_RES) {
		struct pdu_lmp_features *feat = (struct pdu_lmp_features *)pdu;
		memcpy(conn->info.remote_features[0], feat->features, 8);
		conn->info.remote_feat_rec |= BIT(0);

		LOG_DBG("Remote features: %02x %02x %02x %02x %02x %02x %02x %02x",
			conn->info.remote_features[0][0],
			conn->info.remote_features[0][1],
			conn->info.remote_features[0][2],
			conn->info.remote_features[0][3],
			conn->info.remote_features[0][4],
			conn->info.remote_features[0][5],
			conn->info.remote_features[0][6],
			conn->info.remote_features[0][7]);
	}
}

/*
 * Name Request
 * Bluetooth Core Spec Vol 2, Part C - Name request procedure
 */
int lmp_proc_name_request(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_name_req pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.opcode = LMP_NAME_REQ;
	pdu.name_offset = 0;

	conn->req.loc_name_req = true;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));

	return 0;
}

void lmp_proc_name_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	if (hdr->opcode == LMP_NAME_RES) {
		struct pdu_lmp_name_res *res = (struct pdu_lmp_name_res *)pdu;
		uint8_t offset = res->name_offset;
		uint8_t frag_len = MIN(14, res->name_length - offset);

		memcpy(&conn->info.remote_name[offset], res->name_frag, frag_len);
		conn->info.remote_name_len = res->name_length;

		if (offset + frag_len < res->name_length) {
			/* Request next fragment */
			uint8_t link_id = ull_bredr_conn_handle_get(conn);
			struct pdu_lmp_name_req req_pdu;

			req_pdu.hdr.tid = conn->link.role;
			req_pdu.hdr.opcode = LMP_NAME_REQ;
			req_pdu.name_offset = offset + frag_len;

			ull_bredr_send_lmp(link_id, &req_pdu, sizeof(req_pdu));
		} else {
			LOG_INF("Remote name: %s", conn->info.remote_name);
			conn->info.send_remote_name_cfm = true;
		}
	}
}

/*
 * Clock Offset
 */
int lmp_proc_clock_offset_request(struct ull_bredr_conn *conn)
{
	struct pdu_lmp_header pdu;

	pdu.tid = 0;
	pdu.opcode = LMP_CLKOFFSET_REQ;

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

void lmp_proc_clock_offset_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	if (hdr->opcode == LMP_CLKOFFSET_RES) {
		struct pdu_lmp_clkoffset *res = (struct pdu_lmp_clkoffset *)pdu;
		conn->lll.clock_offset = sys_le16_to_cpu(res->clock_offset);

		LOG_DBG("Clock offset: 0x%04x", conn->lll.clock_offset);
	}
}

/*
 * Authentication Procedures
 */
int lmp_proc_auth_initiate(struct ull_bredr_conn *conn)
{
	struct pdu_lmp_au_rand pdu;
	int err;

	/* Generate random challenge */
	err = sys_csrand_get(pdu.random_number, 16);
	if (err) {
		return err;
	}

	pdu.hdr.tid = LMP_TID_INITIATOR;
	pdu.hdr.opcode = LMP_AU_RAND;

	conn->auth_state = LMP_AUTH_STATE_WAIT_SRES;

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

int lmp_proc_auth_respond(struct ull_bredr_conn *conn, uint8_t *au_rand)
{
	struct pdu_lmp_sres pdu;
	uint8_t sres[4];
	uint8_t aco[12];
	int err;

	/* Compute SRES using link key */
	err = lmp_proc_auth_compute_sres(conn->link_key, au_rand,
					 conn->lll.bd_addr, sres, aco);
	if (err) {
		return err;
	}

	pdu.hdr.tid = LMP_TID_RESPONDER;
	pdu.hdr.opcode = LMP_SRES;
	memcpy(pdu.auth_res, sres, 4);

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

void lmp_proc_auth_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	switch (hdr->opcode) {
	case LMP_AU_RAND: {
		struct pdu_lmp_au_rand *req = (struct pdu_lmp_au_rand *)pdu;
		lmp_proc_auth_respond(conn, req->random_number);
		break;
	}

	case LMP_SRES: {
		struct pdu_lmp_sres *res = (struct pdu_lmp_sres *)pdu;
		uint8_t expected_sres[4];
		uint8_t aco[12];

		/* Verify SRES */
		lmp_proc_auth_compute_sres(conn->link_key, conn->auth_au_rand,
					   conn->lll.bd_addr, expected_sres, aco);

		if (memcmp(res->auth_res, expected_sres, 4) == 0) {
			/* SRES matches - authentication successful */
			conn->auth_state = LMP_AUTH_STATE_COMPLETE;
			memcpy(conn->auth_aco, aco, 12);
			LOG_DBG("Authentication complete - SRES verified");
		} else {
			/* SRES mismatch - authentication failed */
			conn->auth_state = LMP_AUTH_STATE_FAILED;
			LOG_WRN("Authentication failed - SRES mismatch");
		}
		break;
	}

	default:
		break;
	}
}

int lmp_proc_auth_compute_sres(uint8_t *link_key, uint8_t *au_rand,
			       uint8_t *bd_addr, uint8_t *sres, uint8_t *aco)
{
	/* E1 algorithm - simplified placeholder
	 * Real implementation requires SAFER+ block cipher
	 */
	uint8_t temp[16];

	/* XOR link_key with au_rand as placeholder */
	for (int i = 0; i < 16; i++) {
		temp[i] = link_key[i] ^ au_rand[i];
	}

	/* SRES is first 4 bytes */
	memcpy(sres, temp, 4);

	/* ACO is bytes 4-15 */
	memcpy(aco, &temp[4], 12);

	return 0;
}

/*
 * Legacy Pairing
 */
int lmp_proc_pairing_initiate(struct ull_bredr_conn *conn)
{
	struct pdu_lmp_au_rand pdu;
	int err;

	/* Generate IN_RAND */
	err = sys_csrand_get(pdu.random_number, 16);
	if (err) {
		return err;
	}

	pdu.hdr.tid = LMP_TID_INITIATOR;
	pdu.hdr.opcode = LMP_IN_RAND;

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

void lmp_proc_pairing_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	switch (hdr->opcode) {
	case LMP_IN_RAND: {
		struct pdu_lmp_au_rand *in_rand_pdu = (struct pdu_lmp_au_rand *)pdu;

		/* Received IN_RAND - need PIN from host */
		LOG_DBG("IN_RAND received, requesting PIN");

		/* Store IN_RAND for later use */
		memcpy(conn->pairing_in_rand, in_rand_pdu->random_number, 16);

		/* Generate HCI PIN Code Request event */
		struct net_buf *buf;
		struct bt_hci_evt_pin_code_req *evt;

		buf = bt_buf_get_evt(BT_HCI_EVT_PIN_CODE_REQ, false, K_NO_WAIT);
		if (buf) {
			evt = net_buf_add(buf, sizeof(*evt));
			memcpy(evt->bdaddr.val, conn->lll.bd_addr, 6);
			bt_recv(buf);
		}
		break;
	}

	case LMP_COMB_KEY: {
		struct {
			struct pdu_lmp_header hdr;
			uint8_t random[16];
		} __packed *comb_key_pdu = (void *)pdu;

		/* Received combination key contribution */
		LOG_DBG("COMB_KEY received");

		/* Compute final link key using E21 algorithm */
		/* Link key = init_key XOR (local_comb_key XOR remote_comb_key) */
		for (int i = 0; i < 16; i++) {
			conn->link_key[i] = conn->pairing_init_key[i] ^
					    conn->pairing_comb_key[i] ^
					    comb_key_pdu->random[i];
		}

		conn->link_key_type = LMP_KEY_TYPE_COMBINATION;
		LOG_DBG("Link key computed");

		/* Send link key notification to host */
		struct net_buf *buf;
		struct bt_hci_evt_link_key_notify *evt;

		buf = bt_buf_get_evt(BT_HCI_EVT_LINK_KEY_NOTIFY, false, K_NO_WAIT);
		if (buf) {
			evt = net_buf_add(buf, sizeof(*evt));
			memcpy(evt->bdaddr.val, conn->lll.bd_addr, 6);
			memcpy(evt->link_key, conn->link_key, 16);
			evt->key_type = conn->link_key_type;
			bt_recv(buf);
		}
		break;
	}

	default:
		break;
	}
}

int lmp_proc_pairing_compute_init_key(uint8_t *pin, uint8_t pin_len,
				      uint8_t *bd_addr, uint8_t *in_rand,
				      uint8_t *init_key)
{
	/* E22 algorithm - simplified placeholder */
	uint8_t temp[16];

	memset(temp, 0, 16);
	memcpy(temp, pin, MIN(pin_len, 16));

	for (int i = 0; i < 16; i++) {
		init_key[i] = temp[i] ^ in_rand[i];
	}

	return 0;
}

int lmp_proc_pairing_compute_comb_key(uint8_t *init_key, uint8_t *comb_rand,
				      uint8_t *comb_key)
{
	/* E21 algorithm - simplified placeholder */
	for (int i = 0; i < 16; i++) {
		comb_key[i] = init_key[i] ^ comb_rand[i];
	}

	return 0;
}

/*
 * Encryption Procedures
 */
int lmp_proc_encryption_start(struct ull_bredr_conn *conn)
{
	return ull_bredr_lmp_encryption_mode_req(conn, BREDR_ENCRYPTION_E0);
}

int lmp_proc_encryption_stop(struct ull_bredr_conn *conn)
{
	return ull_bredr_lmp_stop_encryption_req(conn);
}

void lmp_proc_start_enc(struct ull_bredr_conn *conn)
{
	/* Start encryption procedure
	 * Send LMP_encryption_mode_req to initiate encryption
	 */
	conn->req.loc_enc_req = true;
	conn->link.initiator = true;

	/* Send encryption mode request */
	ull_bredr_lmp_encryption_mode_req(conn, BREDR_ENCRYPTION_E0);

	/* Update LC state */
	conn->lc_state = BREDR_LC_WAIT_ENC_MODE_CFM;

	/* Start LMP timeout */
	lmp_proc_start_lmp_to(conn);

	LOG_DBG("Start encryption initiated: handle=%u", conn->lll.handle);
}

int lmp_proc_encryption_pause(struct ull_bredr_conn *conn)
{
	struct pdu_lmp_ext_header pdu;

	pdu.tid = LMP_TID_INITIATOR;
	pdu.escape = LMP_ESCAPE_4;
	pdu.ext_opcode = LMP_EXT_PAUSE_ENCRYPTION_REQ;

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

int lmp_proc_encryption_resume(struct ull_bredr_conn *conn)
{
	struct pdu_lmp_ext_header pdu;

	pdu.tid = LMP_TID_INITIATOR;
	pdu.escape = LMP_ESCAPE_4;
	pdu.ext_opcode = LMP_EXT_RESUME_ENCRYPTION_REQ;

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

void lmp_proc_encryption_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	switch (hdr->opcode) {
	case LMP_ENCRYPTION_MODE_REQ: {
		struct pdu_lmp_encryption_mode_req *req =
			(struct pdu_lmp_encryption_mode_req *)pdu;
		struct pdu_lmp_accepted acc;

		LOG_DBG("Encryption mode request: %u", req->encryption_mode);

		/* Accept the encryption mode */
		acc.hdr.tid = hdr->tid;
		acc.hdr.opcode = LMP_ACCEPTED;
		acc.opcode = LMP_ENCRYPTION_MODE_REQ;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));
		break;
	}

	case LMP_ENCRYPTION_KEY_SIZE_REQ: {
		struct pdu_lmp_encryption_key_size_req *req =
			(struct pdu_lmp_encryption_key_size_req *)pdu;
		struct pdu_lmp_accepted acc;

		LOG_DBG("Encryption key size request: %u", req->key_size);

		conn->lll.encryption_key_size = req->key_size;

		acc.hdr.tid = hdr->tid;
		acc.hdr.opcode = LMP_ACCEPTED;
		acc.opcode = LMP_ENCRYPTION_KEY_SIZE_REQ;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));
		break;
	}

	case LMP_START_ENCRYPTION_REQ: {
		struct pdu_lmp_start_encryption_req *req =
			(struct pdu_lmp_start_encryption_req *)pdu;
		struct pdu_lmp_accepted acc;
		uint8_t enc_key[16];

		LOG_DBG("Start encryption request");

		/* Compute encryption key using ACO from authentication */
		lmp_proc_encryption_compute_key(conn->link_key,
						req->random_number,
						conn->auth_aco,
						conn->lll.encryption_key_size,
						enc_key);

		/* Start encryption in LLL */
		lll_bredr_encrypt_start(&conn->lll, enc_key,
					conn->lll.encryption_key_size);

		acc.hdr.tid = hdr->tid;
		acc.hdr.opcode = LMP_ACCEPTED;
		acc.opcode = LMP_START_ENCRYPTION_REQ;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));
		break;
	}

	case LMP_STOP_ENCRYPTION_REQ: {
		struct pdu_lmp_accepted acc;

		LOG_DBG("Stop encryption request");

		lll_bredr_encrypt_stop(&conn->lll);

		acc.hdr.tid = hdr->tid;
		acc.hdr.opcode = LMP_ACCEPTED;
		acc.opcode = LMP_STOP_ENCRYPTION_REQ;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));
		break;
	}

	default:
		break;
	}
}

int lmp_proc_encryption_compute_key(uint8_t *link_key, uint8_t *en_rand,
				    uint8_t *aco, uint8_t key_size,
				    uint8_t *encryption_key)
{
	/* E3 algorithm - simplified placeholder
	 * Real implementation requires SAFER+ block cipher
	 */
	uint8_t temp[16];

	/* XOR link_key with en_rand */
	for (int i = 0; i < 16; i++) {
		temp[i] = link_key[i] ^ en_rand[i];
	}

	/* XOR with ACO */
	for (int i = 0; i < 12; i++) {
		temp[i] ^= aco[i];
	}

	/* Truncate to key_size */
	memcpy(encryption_key, temp, key_size);
	memset(&encryption_key[key_size], 0, 16 - key_size);

	return 0;
}

/*
 * Secure Simple Pairing Procedures
 */
int lmp_proc_ssp_initiate(struct ull_bredr_conn *conn)
{
	return ull_bredr_lmp_io_capability_req(conn,
					       conn->io_capability,
					       conn->oob_data_present,
					       conn->auth_requirements);
}

int lmp_proc_ssp_respond(struct ull_bredr_conn *conn)
{
	return ull_bredr_lmp_io_capability_res(conn,
					       conn->io_capability,
					       conn->oob_data_present,
					       conn->auth_requirements);
}

void lmp_proc_ssp_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_ext_header *ext_hdr = (struct pdu_lmp_ext_header *)pdu;

	if (ext_hdr->escape != LMP_ESCAPE_4) {
		return;
	}

	switch (ext_hdr->ext_opcode) {
	case LMP_EXT_IO_CAPABILITY_REQ:
	case LMP_EXT_IO_CAPABILITY_RES: {
		struct pdu_lmp_io_capability *io_cap =
			(struct pdu_lmp_io_capability *)pdu;

		LOG_DBG("IO capability: io=%u, oob=%u, auth=%u",
			io_cap->io_capability, io_cap->oob_data_present,
			io_cap->auth_requirements);

		/* Store remote IO capabilities */
		conn->ssp_state = LMP_SSP_STATE_WAIT_PUBLIC_KEY;

		/* Determine association model */
		int model = lmp_proc_ssp_determine_model(
			conn->io_capability, io_cap->io_capability,
			conn->oob_data_present, io_cap->oob_data_present,
			conn->auth_requirements, io_cap->auth_requirements);

		LOG_DBG("SSP association model: %d", model);

		/* Respond if this was a request */
		if (ext_hdr->ext_opcode == LMP_EXT_IO_CAPABILITY_REQ) {
			lmp_proc_ssp_respond(conn);
		}
		break;
	}

	case LMP_EXT_NUMERIC_COMPARISON_FAILED:
		LOG_WRN("Numeric comparison failed");
		conn->ssp_state = LMP_SSP_STATE_FAILED;
		break;

	case LMP_EXT_PASSKEY_FAILED:
		LOG_WRN("Passkey entry failed");
		conn->ssp_state = LMP_SSP_STATE_FAILED;
		break;

	case LMP_EXT_OOB_FAILED:
		LOG_WRN("OOB authentication failed");
		conn->ssp_state = LMP_SSP_STATE_FAILED;
		break;

	case LMP_EXT_KEYPRESS_NOTIFICATION: {
		/* Forward to host */
		LOG_DBG("Keypress notification received");
		break;
	}

	default:
		break;
	}
}

int lmp_proc_ssp_determine_model(uint8_t local_io, uint8_t remote_io,
				 uint8_t local_oob, uint8_t remote_oob,
				 uint8_t local_auth, uint8_t remote_auth)
{
	/* Check for OOB */
	if (local_oob != LMP_OOB_NOT_PRESENT || remote_oob != LMP_OOB_NOT_PRESENT) {
		return LMP_SSP_MODEL_OOB;
	}

	/* Check MITM requirements */
	bool mitm_required = (local_auth & 0x01) || (remote_auth & 0x01);

	if (!mitm_required) {
		return LMP_SSP_MODEL_JUST_WORKS;
	}

	/* IO capability mapping table for MITM */
	/* DisplayOnly=0, DisplayYesNo=1, KeyboardOnly=2, NoInputNoOutput=3 */
	static const uint8_t model_table[4][4] = {
		/* Remote: DisplayOnly, DisplayYesNo, KeyboardOnly, NoIO */
		/* Local DisplayOnly */
		{LMP_SSP_MODEL_JUST_WORKS, LMP_SSP_MODEL_JUST_WORKS,
		 LMP_SSP_MODEL_PASSKEY_ENTRY, LMP_SSP_MODEL_JUST_WORKS},
		/* Local DisplayYesNo */
		{LMP_SSP_MODEL_JUST_WORKS, LMP_SSP_MODEL_NUMERIC_COMPARISON,
		 LMP_SSP_MODEL_PASSKEY_ENTRY, LMP_SSP_MODEL_JUST_WORKS},
		/* Local KeyboardOnly */
		{LMP_SSP_MODEL_PASSKEY_ENTRY, LMP_SSP_MODEL_PASSKEY_ENTRY,
		 LMP_SSP_MODEL_PASSKEY_ENTRY, LMP_SSP_MODEL_JUST_WORKS},
		/* Local NoInputNoOutput */
		{LMP_SSP_MODEL_JUST_WORKS, LMP_SSP_MODEL_JUST_WORKS,
		 LMP_SSP_MODEL_JUST_WORKS, LMP_SSP_MODEL_JUST_WORKS},
	};

	if (local_io > 3 || remote_io > 3) {
		return LMP_SSP_MODEL_JUST_WORKS;
	}

	return model_table[local_io][remote_io];
}

int lmp_proc_user_confirm_reply(struct ull_bredr_conn *conn, uint8_t accept)
{
	if (!accept) {
		/* Send numeric comparison failed */
		struct pdu_lmp_ext_header pdu;

		pdu.tid = LMP_TID_INITIATOR;
		pdu.escape = LMP_ESCAPE_4;
		pdu.ext_opcode = LMP_EXT_NUMERIC_COMPARISON_FAILED;

		return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
	}

	/* Continue with DHKey check */
	/* Compute DHKey check value using f3 function */
	uint8_t dhkey_check[16];
	uint8_t io_cap[3];

	io_cap[0] = conn->sp.io_cap_loc[0];
	io_cap[1] = conn->sp.io_cap_loc[1];
	io_cap[2] = conn->sp.io_cap_loc[2];

	lmp_proc_ssp_compute_dhkey_check(conn->sp.dhkey, conn->sp.nonce_loc,
					 conn->info.local_bd_addr, io_cap,
					 dhkey_check);

	/* Send LMP_DHKey_Check */
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	ull_bredr_send_pdu_dhkey_chk(link_id, dhkey_check, conn->link.role);

	conn->ssp_state = LMP_SSP_STATE_WAIT_DHKEY_CHECK;

	return 0;
}

int lmp_proc_user_passkey_reply(struct ull_bredr_conn *conn, uint32_t passkey)
{
	/* Store passkey for authentication */
	conn->sp.passkey = passkey;
	conn->sp.passkey_bit = 0;

	/* Use passkey in commitment computation for passkey entry */
	/* The passkey is used bit-by-bit in the commitment exchange */
	LOG_DBG("Passkey stored: %06u", passkey);

	/* Start passkey commitment exchange */
	lmp_proc_start_passkey_loop(conn);

	return 0;
}

void lmp_proc_start_passkey_loop(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	uint8_t commitment[16];
	uint8_t z;

	/* Passkey entry uses 20 rounds of commitment exchange
	 * Each round uses one bit of the passkey
	 */
	if (conn->sp.passkey_bit >= 20) {
		/* All 20 bits exchanged - proceed to DHKey check */
		LOG_DBG("Passkey loop complete");
		conn->ssp_state = LMP_SSP_STATE_WAIT_DHKEY_CHECK;
		return;
	}

	/* Get current passkey bit */
	z = (conn->sp.passkey >> conn->sp.passkey_bit) & 0x01;

	/* Compute commitment for this round */
	lmp_proc_ssp_compute_confirm(conn->sp.loc_rand_n, NULL,
				     conn->sp.nonce_loc, z, commitment);

	/* Store local commitment */
	memcpy(conn->sp.loc_commitment, commitment, 16);

	/* Send LMP_SP_Confirm */
	ull_bredr_send_pdu_sp_cfm(link_id, commitment, conn->link.role);

	/* Update state */
	conn->lc_state = BREDR_LC_WAIT_PASSKEY_COMMIT_RSP;

	LOG_DBG("Passkey loop: bit=%u, z=%u", conn->sp.passkey_bit, z);
}

int lmp_proc_user_passkey_negative_reply(struct ull_bredr_conn *conn)
{
	struct pdu_lmp_ext_header pdu;

	pdu.tid = LMP_TID_INITIATOR;
	pdu.escape = LMP_ESCAPE_4;
	pdu.ext_opcode = LMP_EXT_PASSKEY_FAILED;

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

/* SSP cryptographic functions - placeholders */
int lmp_proc_ssp_compute_confirm(uint8_t *public_key_x, uint8_t *public_key_y,
				 uint8_t *nonce, uint8_t z, uint8_t *confirm)
{
	/* f1 function using HMAC-SHA256 - placeholder */
	memset(confirm, 0, 16);
	return 0;
}

int lmp_proc_ssp_compute_dhkey(uint8_t *local_private_key,
			       uint8_t *remote_public_key, uint8_t *dhkey)
{
	/* P-256 ECDH - placeholder */
	memset(dhkey, 0, 32);
	return 0;
}

int lmp_proc_ssp_compute_link_key(uint8_t *dhkey, uint8_t *nonce_a,
				  uint8_t *nonce_b, uint8_t *bd_addr_a,
				  uint8_t *bd_addr_b, uint8_t *link_key)
{
	/* f2 function using HMAC-SHA256 - placeholder */
	memset(link_key, 0, 16);
	return 0;
}

int lmp_proc_ssp_compute_dhkey_check(uint8_t *dhkey, uint8_t *nonce,
				     uint8_t *bd_addr, uint8_t *io_cap,
				     uint8_t *check)
{
	/* f3 function using HMAC-SHA256 - placeholder */
	memset(check, 0, 16);
	return 0;
}

/*
 * Power Mode Procedures
 */
int lmp_proc_hold_mode(struct ull_bredr_conn *conn, uint16_t hold_time)
{
	return ull_bredr_lmp_hold_req(conn, hold_time, 0);
}

int lmp_proc_sniff_mode(struct ull_bredr_conn *conn, uint16_t interval,
			uint16_t attempt, uint16_t timeout)
{
	return ull_bredr_lmp_sniff_req(conn, 0, interval, attempt, timeout);
}

int lmp_proc_exit_sniff_mode(struct ull_bredr_conn *conn)
{
	return ull_bredr_lmp_unsniff_req(conn);
}

int lmp_proc_park_mode(struct ull_bredr_conn *conn)
{
	return ull_bredr_lmp_park_req(conn);
}

int lmp_proc_exit_park_mode(struct ull_bredr_conn *conn)
{
	return ull_bredr_lmp_unpark_req(conn);
}

void lmp_proc_power_mode_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	switch (hdr->opcode) {
	case LMP_HOLD:
	case LMP_HOLD_REQ: {
		struct pdu_lmp_hold *hold = (struct pdu_lmp_hold *)pdu;
		struct pdu_lmp_accepted acc;

		LOG_DBG("Hold request: time=%u, instant=%u",
			sys_le16_to_cpu(hold->hold_time),
			sys_le32_to_cpu(hold->hold_instant));

		conn->power_mode = 1;  /* Hold mode */

		acc.hdr.tid = hdr->tid;
		acc.hdr.opcode = LMP_ACCEPTED;
		acc.opcode = hdr->opcode;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));
		break;
	}

	case LMP_SNIFF_REQ: {
		struct pdu_lmp_sniff_req *sniff = (struct pdu_lmp_sniff_req *)pdu;
		struct pdu_lmp_accepted acc;

		LOG_DBG("Sniff request: d=%u, t=%u, attempt=%u, timeout=%u",
			sys_le16_to_cpu(sniff->d_sniff),
			sys_le16_to_cpu(sniff->t_sniff),
			sys_le16_to_cpu(sniff->sniff_attempt),
			sys_le16_to_cpu(sniff->sniff_timeout));

		conn->power_mode = 2;  /* Sniff mode */
		conn->sniff_interval = sys_le16_to_cpu(sniff->t_sniff);
		conn->sniff_attempt = sys_le16_to_cpu(sniff->sniff_attempt);
		conn->sniff_timeout = sys_le16_to_cpu(sniff->sniff_timeout);

		acc.hdr.tid = hdr->tid;
		acc.hdr.opcode = LMP_ACCEPTED;
		acc.opcode = LMP_SNIFF_REQ;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));
		break;
	}

	case LMP_UNSNIFF_REQ: {
		struct pdu_lmp_accepted acc;

		LOG_DBG("Unsniff request");

		conn->power_mode = 0;  /* Active mode */

		acc.hdr.tid = hdr->tid;
		acc.hdr.opcode = LMP_ACCEPTED;
		acc.opcode = LMP_UNSNIFF_REQ;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));
		break;
	}

	default:
		break;
	}
}

/*
 * Role Switch Procedure
 */
int lmp_proc_role_switch(struct ull_bredr_conn *conn)
{
	/* Calculate switch instant - at least 2*Tpoll in the future */
	uint32_t instant = conn->link.poll_interval * 2;
	if (instant < 32) {
		instant = 32;  /* Minimum 32 slots */
	}

	return ull_bredr_lmp_switch_req(conn, instant);
}

void lmp_proc_role_switch_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	if (hdr->opcode == LMP_SWITCH_REQ) {
		struct pdu_lmp_switch_req *req = (struct pdu_lmp_switch_req *)pdu;
		struct pdu_lmp_accepted acc;

		LOG_DBG("Switch request: instant=%u",
			sys_le32_to_cpu(req->switch_instant));

		conn->role_switch_pending = 1;

		acc.hdr.tid = hdr->tid;
		acc.hdr.opcode = LMP_ACCEPTED;
		acc.opcode = LMP_SWITCH_REQ;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));
	}
}

/*
 * QoS Procedure
 */
int lmp_proc_qos_setup(struct ull_bredr_conn *conn, uint16_t poll_interval)
{
	return ull_bredr_lmp_qos_req(conn, poll_interval, 1);
}

void lmp_proc_qos_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	if (hdr->opcode == LMP_QUALITY_OF_SERVICE_REQ) {
		struct pdu_lmp_accepted acc;

		LOG_DBG("QoS request received");

		acc.hdr.tid = hdr->tid;
		acc.hdr.opcode = LMP_ACCEPTED;
		acc.opcode = LMP_QUALITY_OF_SERVICE_REQ;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));
	}
}

/*
 * AFH Procedure
 */
int lmp_proc_afh_set_channel_map(struct ull_bredr_conn *conn, uint8_t *map)
{
	/* Calculate AFH instant - at least 6*Tpoll in the future */
	uint32_t instant = conn->link.poll_interval * 6;
	if (instant < 96) {
		instant = 96;  /* Minimum 96 slots per spec */
	}

	return ull_bredr_lmp_set_afh(conn, instant, 1, map);
}

void lmp_proc_afh_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	if (hdr->opcode == LMP_SET_AFH) {
		struct pdu_lmp_set_afh *afh = (struct pdu_lmp_set_afh *)pdu;

		LOG_DBG("Set AFH: instant=%u, mode=%u",
			sys_le32_to_cpu(afh->afh_instant), afh->afh_mode);

		conn->lll.afh_enabled = afh->afh_mode;
		memcpy(conn->lll.afh_channel_map, afh->afh_channel_map, 10);

		/* Count used channels */
		conn->lll.afh_channel_count = 0;
		for (int i = 0; i < 79; i++) {
			if (afh->afh_channel_map[i / 8] & BIT(i % 8)) {
				conn->lll.afh_channel_count++;
			}
		}
	}
}

/*
 * SCO Procedures
 */
int lmp_proc_sco_setup(struct ull_bredr_conn *conn, uint8_t sco_handle,
		       uint8_t d_sco, uint8_t t_sco, uint8_t packet_type,
		       uint8_t air_mode)
{
	return ull_bredr_lmp_sco_link_req(conn, sco_handle, d_sco, t_sco,
					  packet_type, air_mode);
}

int lmp_proc_sco_remove(struct ull_bredr_conn *conn, uint8_t sco_handle,
			uint8_t reason)
{
	return ull_bredr_lmp_remove_sco_link_req(conn, sco_handle, reason);
}

void lmp_proc_sco_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	switch (hdr->opcode) {
	case LMP_SCO_LINK_REQ: {
		struct pdu_lmp_sco_link_req *req =
			(struct pdu_lmp_sco_link_req *)pdu;
		struct pdu_lmp_accepted acc;
		struct ull_bredr_sco *sco;

		LOG_DBG("SCO link request: handle=%u, d=%u, t=%u, pkt=%u, air=%u",
			req->sco_handle, req->d_sco, req->t_sco,
			req->sco_packet, req->air_mode);

		/* Allocate SCO connection */
		sco = ull_bredr_sco_acquire();
		if (!sco) {
			/* No resources - reject */
			struct pdu_lmp_not_accepted rej;

			rej.hdr.tid = hdr->tid;
			rej.hdr.opcode = LMP_NOT_ACCEPTED;
			rej.opcode = LMP_SCO_LINK_REQ;
			rej.error_code = BT_HCI_ERR_CONN_LIMIT_EXCEEDED;

			ull_bredr_tx_enqueue(conn, &rej, sizeof(rej));
			break;
		}

		/* Configure SCO connection */
		sco->lll.handle = req->sco_handle;
		sco->lll.d_sco = req->d_sco;
		sco->lll.t_sco = req->t_sco;
		sco->lll.packet_type = req->sco_packet;
		sco->lll.air_mode = req->air_mode;
		sco->lll.role = conn->lll.role;
		memcpy(sco->lll.bd_addr, conn->lll.bd_addr, 6);
		sco->acl_handle = conn->lll.handle;

		/* Start SCO */
		ull_bredr_sco_start(sco, req->t_sco);

		acc.hdr.tid = hdr->tid;
		acc.hdr.opcode = LMP_ACCEPTED;
		acc.opcode = LMP_SCO_LINK_REQ;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));
		break;
	}

	case LMP_REMOVE_SCO_LINK_REQ: {
		struct pdu_lmp_remove_sco_link_req *req =
			(struct pdu_lmp_remove_sco_link_req *)pdu;
		struct pdu_lmp_accepted acc;

		LOG_DBG("Remove SCO link: handle=%u, reason=0x%02x",
			req->sco_handle, req->reason);

		/* Find and stop SCO connection */
		for (int i = 0; i < ULL_BREDR_SCO_MAX; i++) {
			struct ull_bredr_sco *sco = ull_bredr_sco_get(i);
			if (sco && sco->lll.handle == req->sco_handle) {
				ull_bredr_sco_stop(sco);
				ull_bredr_sco_release(sco);
				break;
			}
		}

		acc.hdr.tid = hdr->tid;
		acc.hdr.opcode = LMP_ACCEPTED;
		acc.opcode = LMP_REMOVE_SCO_LINK_REQ;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));
		break;
	}

	default:
		break;
	}
}

/*
 * eSCO Procedures
 */
int lmp_proc_esco_setup(struct ull_bredr_conn *conn,
			struct pdu_lmp_esco_link_req *params)
{
	return ull_bredr_lmp_esco_link_req(conn, params);
}

int lmp_proc_esco_modify(struct ull_bredr_conn *conn,
			 struct pdu_lmp_esco_link_req *params)
{
	/* Same PDU, different negotiation state */
	return ull_bredr_lmp_esco_link_req(conn, params);
}

int lmp_proc_esco_remove(struct ull_bredr_conn *conn, uint8_t esco_handle,
			 uint8_t reason)
{
	return ull_bredr_lmp_remove_esco_link_req(conn, esco_handle, reason);
}

void lmp_proc_esco_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_ext_header *ext_hdr = (struct pdu_lmp_ext_header *)pdu;

	if (ext_hdr->escape != LMP_ESCAPE_4) {
		return;
	}

	switch (ext_hdr->ext_opcode) {
	case LMP_EXT_ESCO_LINK_REQ: {
		struct pdu_lmp_esco_link_req *req =
			(struct pdu_lmp_esco_link_req *)pdu;
		struct pdu_lmp_ext_accepted acc;
		struct ull_bredr_esco *esco;

		LOG_DBG("eSCO link request: handle=%u, lt_addr=%u",
			req->esco_handle, req->esco_lt_addr);

		/* Allocate eSCO connection */
		esco = ull_bredr_esco_acquire();
		if (!esco) {
			/* No resources - reject */
			struct pdu_lmp_ext_not_accepted rej;

			rej.hdr.tid = ext_hdr->tid;
			rej.hdr.escape = LMP_ESCAPE_4;
			rej.hdr.ext_opcode = LMP_EXT_NOT_ACCEPTED;
			rej.escape_opcode = LMP_ESCAPE_4;
			rej.ext_opcode = LMP_EXT_ESCO_LINK_REQ;
			rej.error_code = BT_HCI_ERR_CONN_LIMIT_EXCEEDED;

			ull_bredr_tx_enqueue(conn, &rej, sizeof(rej));
			break;
		}

		/* Configure eSCO connection */
		esco->lll.handle = req->esco_handle;
		esco->lll.lt_addr = req->esco_lt_addr;
		esco->lll.t_esco = req->t_esco;
		esco->lll.w_esco = req->w_esco;
		esco->lll.packet_type_m2s = req->esco_packet_type_m_to_s;
		esco->lll.packet_type_s2m = req->esco_packet_type_s_to_m;
		esco->lll.packet_length_m2s = sys_le16_to_cpu(req->packet_length_m_to_s);
		esco->lll.packet_length_s2m = sys_le16_to_cpu(req->packet_length_s_to_m);
		esco->lll.air_mode = req->air_mode;
		esco->lll.role = conn->lll.role;
		memcpy(esco->lll.bd_addr, conn->lll.bd_addr, 6);
		esco->acl_handle = conn->lll.handle;

		/* Start eSCO */
		ull_bredr_esco_start(esco, req->t_esco);

		acc.hdr.tid = ext_hdr->tid;
		acc.hdr.escape = LMP_ESCAPE_4;
		acc.hdr.ext_opcode = LMP_EXT_ACCEPTED;
		acc.escape_opcode = LMP_ESCAPE_4;
		acc.ext_opcode = LMP_EXT_ESCO_LINK_REQ;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));
		break;
	}

	case LMP_EXT_REMOVE_ESCO_LINK_REQ: {
		struct pdu_lmp_remove_esco_link_req *req =
			(struct pdu_lmp_remove_esco_link_req *)pdu;
		struct pdu_lmp_ext_accepted acc;

		LOG_DBG("Remove eSCO link: handle=%u, reason=0x%02x",
			req->esco_handle, req->reason);

		/* Find and stop eSCO connection */
		for (int i = 0; i < ULL_BREDR_ESCO_MAX; i++) {
			struct ull_bredr_esco *esco = ull_bredr_esco_get(i);
			if (esco && esco->lll.handle == req->esco_handle) {
				ull_bredr_esco_stop(esco);
				ull_bredr_esco_release(esco);
				break;
			}
		}

		acc.hdr.tid = ext_hdr->tid;
		acc.hdr.escape = LMP_ESCAPE_4;
		acc.hdr.ext_opcode = LMP_EXT_ACCEPTED;
		acc.escape_opcode = LMP_ESCAPE_4;
		acc.ext_opcode = LMP_EXT_REMOVE_ESCO_LINK_REQ;

		ull_bredr_tx_enqueue(conn, &acc, sizeof(acc));
		break;
	}

	default:
		break;
	}
}

/*
 * Detach Procedure
 */
int lmp_proc_detach(struct ull_bredr_conn *conn, uint8_t reason)
{
	return ull_bredr_lmp_detach(conn, reason);
}

void lmp_proc_detach_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	if (hdr->opcode == LMP_DETACH) {
		struct pdu_lmp_detach *det = (struct pdu_lmp_detach *)pdu;

		LOG_INF("Detach received: reason=0x%02x", det->reason);

		ull_bredr_conn_cleanup(conn, det->reason);
	}
}

/*
 * Supervision Timeout
 */
int lmp_proc_supervision_timeout(struct ull_bredr_conn *conn, uint16_t timeout)
{
	struct pdu_lmp_supervision_timeout pdu;

	pdu.hdr.tid = LMP_TID_INITIATOR;
	pdu.hdr.opcode = LMP_SUPERVISION_TIMEOUT;
	pdu.supervision_timeout = sys_cpu_to_le16(timeout);

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

void lmp_proc_supervision_timeout_rx(struct ull_bredr_conn *conn,
				     uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	if (hdr->opcode == LMP_SUPERVISION_TIMEOUT) {
		struct pdu_lmp_supervision_timeout *st =
			(struct pdu_lmp_supervision_timeout *)pdu;

		conn->supervision_timeout = sys_le16_to_cpu(st->supervision_timeout);
		conn->lll.supervision_timeout = conn->supervision_timeout;

		LOG_DBG("Supervision timeout: %u", conn->supervision_timeout);
	}
}

/*
 * Max Slots
 */
int lmp_proc_max_slots(struct ull_bredr_conn *conn, uint8_t max_slots)
{
	struct {
		struct pdu_lmp_header hdr;
		uint8_t max_slots;
	} __packed pdu;

	pdu.hdr.tid = LMP_TID_INITIATOR;
	pdu.hdr.opcode = LMP_MAX_SLOT;
	pdu.max_slots = max_slots;

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

void lmp_proc_max_slots_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	if (hdr->opcode == LMP_MAX_SLOT || hdr->opcode == LMP_MAX_SLOT_REQ) {
		uint8_t max_slots = pdu[1];

		conn->lll.max_slots = max_slots;
		LOG_DBG("Max slots: %u", max_slots);

		if (hdr->opcode == LMP_MAX_SLOT_REQ) {
			/* Respond with our max slots */
			lmp_proc_max_slots(conn, conn->lll.max_slots);
		}
	}
}

/*
 * Ping
 */
int lmp_proc_ping(struct ull_bredr_conn *conn)
{
	struct pdu_lmp_ext_header pdu;

	pdu.tid = LMP_TID_INITIATOR;
	pdu.escape = LMP_ESCAPE_4;
	pdu.ext_opcode = LMP_EXT_PING_REQ;

	return ull_bredr_tx_enqueue(conn, &pdu, sizeof(pdu));
}

void lmp_proc_ping_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_ext_header *ext_hdr = (struct pdu_lmp_ext_header *)pdu;

	if (ext_hdr->escape != LMP_ESCAPE_4) {
		return;
	}

	if (ext_hdr->ext_opcode == LMP_EXT_PING_REQ) {
		uint8_t link_id = ull_bredr_conn_handle_get(conn);
		struct pdu_lmp_ext_header res;

		res.tid = ext_hdr->tid;
		res.escape = LMP_ESCAPE_4;
		res.ext_opcode = LMP_EXT_PING_RES;

		ull_bredr_send_lmp(link_id, &res, sizeof(res));
	}
}

/*
 * Authentication Completion
 * Bluetooth Core Spec Vol 2, Part C - Authentication procedure
 */
void lmp_proc_auth_cmp(struct ull_bredr_conn *conn, uint8_t reason)
{
	if ((reason == 0) || ((reason != 0) && conn->link.setup_complete)) {
		if (reason == 0) {
			conn->enc.link_key_valid = true;
		}

		/* Reset local authentication request */
		conn->req.loc_auth_req = false;
		conn->link.initiator = false;

		switch (reason) {
		case 0:  /* CO_ERROR_NO_ERROR */
			if (conn->enc.enc_mode != ENC_DISABLED) {
				conn->epr.on = true;
				conn->req.loc_enc_key_refresh = true;
				conn->link.initiator = true;
			}

			if (conn->req.loc_enc_req) {
				/* Start encryption */
				lmp_proc_start_enc(conn);
			} else if (conn->req.loc_enc_key_refresh) {
				/* Start EPR */
				lmp_proc_initiator_epr(conn);
			}
			conn->lc_state = BREDR_LC_CONNECTED;
			break;

		case BT_HCI_ERR_CONN_TIMEOUT:
			lmp_proc_detach(conn, reason);
			break;

		default:
			if (conn->req.loc_enc_req) {
				lmp_proc_send_enc_chg_evt(
					ull_bredr_conn_handle_get(conn), reason);
				conn->req.loc_enc_req = false;
			}
			conn->lc_state = BREDR_LC_CONNECTED;
			break;
		}
	} else {
		lmp_proc_detach(conn, reason);
	}
}

/*
 * Link Key Calculation
 * Bluetooth Core Spec Vol 2, Part H - Link key derivation
 */
void lmp_proc_calc_link_key(struct ull_bredr_conn *conn)
{
	conn->enc.key_status = KEY_PRESENT;

	/* Compute link key using f2 function (HMAC-SHA256 based)
	 * Link key = f2(DHKey, Na, Nb, "btlk", BD_ADDR_A, BD_ADDR_B)
	 *
	 * For now, use simplified computation - real implementation
	 * requires HMAC-SHA256 as specified in Core Spec Vol 2, Part H
	 */
	lmp_proc_ssp_compute_link_key(conn->sp.dhkey,
				      conn->sp.nonce_loc,
				      conn->sp.nonce_rem,
				      conn->info.local_bd_addr,
				      conn->lll.bd_addr,
				      conn->link_key);

	/* Set link key type based on authentication */
	if (conn->sp.sec_con) {
		conn->link_key_type = (conn->sp.mitm_protection) ?
			LMP_KEY_TYPE_AUTHENTICATED_P256 :
			LMP_KEY_TYPE_UNAUTHENTICATED_P256;
	} else {
		conn->link_key_type = (conn->sp.mitm_protection) ?
			LMP_KEY_TYPE_AUTHENTICATED_P192 :
			LMP_KEY_TYPE_UNAUTHENTICATED_P192;
	}

	LOG_DBG("Link key calculated: type=%u", conn->link_key_type);

	/* End of simple pairing */
	lmp_proc_sp_end(ull_bredr_conn_handle_get(conn), 0);

	/* Start mutual authentication */
	if (conn->sp.sp_initiator) {
		lmp_proc_init_start_mutual_auth(conn);
	} else {
		conn->lc_state = BREDR_LC_WAIT_AU_RAND_RSP;
	}
}

/*
 * Packet Type Change
 * Bluetooth Core Spec Vol 2, Part C - Packet type negotiation
 */
void lmp_proc_chg_pkt_type_cont(struct ull_bredr_conn *conn, uint8_t status)
{
	if (conn->link.cur_packet_type) {
		uint8_t max_slot = lmp_proc_max_slot(conn->link.cur_packet_type);

		if (max_slot != conn->link.tx_max_slot_cur) {
			/* Send HCI Max Slot Change event */
			struct net_buf *buf;
			struct bt_hci_evt_max_slots_changed *evt;

			buf = bt_buf_get_evt(BT_HCI_EVT_MAX_SLOTS_CHANGED, false, K_NO_WAIT);
			if (buf) {
				evt = net_buf_add(buf, sizeof(*evt));
				evt->handle = sys_cpu_to_le16(ull_bredr_conn_handle_get(conn));
				evt->max_slots = max_slot;
				bt_recv(buf);
			}

			LOG_DBG("Max slot changed: %u -> %u",
				conn->link.tx_max_slot_cur, max_slot);
		}
		conn->link.tx_max_slot_cur = max_slot;
	}
	lmp_proc_chg_pkt_type_cmp(conn, status);
}

void lmp_proc_chg_pkt_type_cmp(struct ull_bredr_conn *conn, uint8_t status)
{
	if (conn->req.loc_cpt_req) {
		/* Send HCI Connection Packet Type Changed event */
		struct net_buf *buf;
		struct bt_hci_evt_conn_pkt_type_changed *evt;

		buf = bt_buf_get_evt(BT_HCI_EVT_CONN_PKT_TYPE_CHANGED, false, K_NO_WAIT);
		if (buf) {
			evt = net_buf_add(buf, sizeof(*evt));
			evt->status = status;
			evt->handle = sys_cpu_to_le16(ull_bredr_conn_handle_get(conn));
			evt->pkt_type = sys_cpu_to_le16(conn->link.cur_packet_type);
			bt_recv(buf);
		}

		LOG_DBG("Packet type change complete: status=%u, pkt_type=0x%04x",
			status, conn->link.cur_packet_type);
		conn->req.loc_cpt_req = false;
	}
	conn->lc_state = BREDR_LC_CONNECTED;
}

void lmp_proc_chg_pkt_type_retry(struct ull_bredr_conn *conn)
{
	/* Check remote features for slot support */
	if (!lmp_proc_get_feature(conn->info.remote_features[0],
				  FEAT_3_SLOT_BIT_POS)) {
		lmp_proc_suppress_acl_packet(&conn->link.cur_packet_type, 0x03);
	}
	if (!lmp_proc_get_feature(conn->info.remote_features[0],
				  FEAT_5_SLOT_BIT_POS)) {
		lmp_proc_suppress_acl_packet(&conn->link.cur_packet_type, 0x05);
	}

	while (conn->link.cur_packet_type) {
		uint8_t max_slot = lmp_proc_max_slot(conn->link.cur_packet_type);

		if (max_slot > conn->link.tx_max_slot_cur) {
			if (conn->link.max_slot_received) {
				if (max_slot > conn->link.max_slot_received) {
					lmp_proc_suppress_acl_packet(
						&conn->link.cur_packet_type, max_slot);
				} else {
					lmp_proc_chg_pkt_type_cont(conn, 0);
					return;
				}
			} else {
				if (max_slot == 0x01) {
					lmp_proc_chg_pkt_type_cont(conn, 0);
					return;
				} else {
					/* Send LMP_MaxSlotReq */
					uint8_t link_id = ull_bredr_conn_handle_get(conn);
					ull_bredr_send_pdu_max_slot_req(link_id, max_slot,
								       conn->link.role);
					lmp_proc_start_lmp_to(conn);
					conn->lc_state = BREDR_LC_WAIT_MAX_SLOT_CFM;
					return;
				}
			}
		} else {
			lmp_proc_chg_pkt_type_cont(conn, 0);
			return;
		}
	}

	if (conn->link.setup_complete) {
		lmp_proc_chg_pkt_type_cont(conn, BT_HCI_ERR_UNSUPP_FEATURE_PARAM_VAL);
	} else {
		lmp_proc_detach(conn, BT_HCI_ERR_UNSUPP_LMP_PARAM_VAL);
	}
}

/*
 * Encryption Completion
 * Bluetooth Core Spec Vol 2, Part C - Encryption procedure
 */
void lmp_proc_enc_cmp(struct ull_bredr_conn *conn, uint8_t reason)
{
	lmp_proc_send_enc_chg_evt(ull_bredr_conn_handle_get(conn), reason);

	conn->req.loc_enc_req = false;
	conn->req.peer_enc_req = false;
	conn->link.initiator = false;

	conn->lc_state = BREDR_LC_CONNECTED;
}

/*
 * Encryption Change Event
 */
void lmp_proc_send_enc_chg_evt(uint8_t idx, uint8_t status)
{
	struct net_buf *buf;
	struct bt_hci_evt_encrypt_change *evt;

	buf = bt_buf_get_evt(BT_HCI_EVT_ENCRYPT_CHANGE, false, K_NO_WAIT);
	if (!buf) {
		LOG_ERR("No buffer for Encryption Change event");
		return;
	}

	evt = net_buf_add(buf, sizeof(*evt));
	evt->status = status;
	evt->handle = sys_cpu_to_le16(idx);
	evt->encrypt = (status == 0) ? 1 : 0;

	bt_recv(buf);

	LOG_DBG("Encryption change event: handle=%u, status=%u, encrypt=%u",
		idx, status, evt->encrypt);
}

/*
 * EPR Functions
 * Bluetooth Core Spec Vol 2, Part C - Encryption Pause Resume
 */
void lmp_proc_epr_cmp(struct ull_bredr_conn *conn)
{
	struct net_buf *buf;
	struct bt_hci_evt_encrypt_key_refresh_complete *evt;

	conn->epr.on = false;
	conn->req.loc_enc_key_refresh = false;
	conn->req.peer_enc_key_refresh = false;

	/* Send HCI Encryption Key Refresh Complete event */
	buf = bt_buf_get_evt(BT_HCI_EVT_ENCRYPT_KEY_REFRESH_COMPLETE, false, K_NO_WAIT);
	if (buf) {
		evt = net_buf_add(buf, sizeof(*evt));
		evt->status = 0;
		evt->handle = sys_cpu_to_le16(ull_bredr_conn_handle_get(conn));
		bt_recv(buf);
	}

	LOG_DBG("EPR complete: handle=%u", conn->lll.handle);

	conn->lc_state = BREDR_LC_CONNECTED;
}

void lmp_proc_initiator_epr(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	if (lmp_proc_get_feature(conn->info.remote_features[0],
				 FEAT_PAUSE_ENCRYPT_BIT_POS)) {
		/* Flow off ACL data during EPR */
		conn->epr.flow_off = true;

		lmp_proc_start_lmp_to(conn);

		if (conn->link.initiator) {
			if (conn->sp.sec_con) {
				/* Send LMP_PauseEncryptionAesReq with random */
				uint8_t random[16];
				sys_csrand_get(random, 16);
				memcpy(conn->epr.random, random, 16);

				/* For AES encryption pause, send extended PDU */
				struct {
					struct pdu_lmp_ext_header hdr;
					uint8_t random[16];
				} __packed pdu;

				pdu.hdr.tid = conn->link.role;
				pdu.hdr.escape = LMP_ESCAPE_4;
				pdu.hdr.ext_opcode = LMP_EXT_PAUSE_ENCRYPTION_AES_REQ;
				memcpy(pdu.random, random, 16);

				ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
			} else {
				/* Send LMP_PauseEncryptionReq */
				ull_bredr_send_pdu_paus_enc_req(link_id, conn->link.role);
			}

			if (conn->link.role == BREDR_ROLE_PERIPHERAL) {
				conn->lc_state = BREDR_LC_WAIT_EPR_STOP_PERIPH;
			} else {
				conn->lc_state = BREDR_LC_WAIT_EPR_PAUSE_CENTRAL;
			}
		} else {
			if (conn->link.role == BREDR_ROLE_PERIPHERAL) {
				conn->lc_state = BREDR_LC_WAIT_EPR_STOP_RSP;
			} else {
				conn->lc_state = BREDR_LC_WAIT_EPR_PAUSE_RSP;
			}
		}
	} else {
		conn->epr.on = false;
		conn->epr.cclk = false;
		lmp_proc_epr_change_lk(conn, 0);
	}
}

void lmp_proc_epr_change_lk(struct ull_bredr_conn *conn, uint8_t reason)
{
	struct net_buf *buf;
	struct bt_hci_evt_link_key_notify *evt;

	/* Send HCI Change Connection Link Key Complete event */
	buf = bt_buf_get_evt(BT_HCI_EVT_LINK_KEY_NOTIFY, false, K_NO_WAIT);
	if (buf) {
		evt = net_buf_add(buf, sizeof(*evt));
		memcpy(evt->bdaddr.val, conn->lll.bd_addr, 6);
		memcpy(evt->link_key, conn->link_key, 16);
		evt->key_type = conn->link_key_type;
		bt_recv(buf);
	}

	LOG_DBG("EPR change link key: handle=%u, reason=%u",
		conn->lll.handle, reason);
}

void lmp_proc_epr_resp(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	/* EPR responder - respond to pause/resume encryption requests */
	if (conn->epr.pause_pending) {
		/* Send LMP_accepted for pause encryption */
		ull_bredr_send_pdu_acc(link_id, LMP_ESCAPE_4, conn->link.role);
		conn->epr.pause_pending = false;

		/* Disable encryption */
		lll_bredr_encrypt_stop(&conn->lll);

		/* Wait for resume */
		conn->lc_state = BREDR_LC_WAIT_EPR_RESUME;
	} else if (conn->epr.resume_pending) {
		/* Send LMP_accepted for resume encryption */
		ull_bredr_send_pdu_acc(link_id, LMP_ESCAPE_4, conn->link.role);
		conn->epr.resume_pending = false;

		/* Re-enable encryption with new key */
		lll_bredr_encrypt_start(&conn->lll, conn->enc.encryption_key,
					conn->lll.encryption_key_size);

		/* EPR complete */
		lmp_proc_epr_cmp(conn);
	}
}

/*
 * Simple Pairing Functions
 */
void lmp_proc_sp_end(uint8_t link_id, uint8_t reason)
{
	/* Free SP computation buffers and clean up SSP state */
	if (link_id < ULL_BREDR_MAX_CONN) {
		struct ull_bredr_conn *conn = ull_bredr_conn_get(link_id);
		if (conn) {
			/* Clear SSP state */
			conn->ssp_state = LMP_SSP_STATE_IDLE;
			memset(&conn->sp, 0, sizeof(conn->sp));
		}
	}

	LOG_DBG("SP end: link_id=%u, reason=%u", link_id, reason);
}

void lmp_proc_sp_fail(struct ull_bredr_conn *conn)
{
	lmp_proc_sp_end(ull_bredr_conn_handle_get(conn),
			BT_HCI_ERR_AUTH_FAIL);
	lmp_proc_auth_cmp(conn, BT_HCI_ERR_AUTH_FAIL);
}

void lmp_proc_init_start_mutual_auth(struct ull_bredr_conn *conn)
{
	/* Start mutual authentication as initiator */
	lmp_proc_auth_initiate(conn);
}

/*
 * Role Switch Functions
 * Bluetooth Core Spec Vol 2, Part C - Role switch procedure
 */
void lmp_proc_local_switch(struct ull_bredr_conn *conn)
{
	uint8_t lt_addr;
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	if (ull_bredr_role_switch_start(link_id, &lt_addr)) {
		/* Calculate switch instant - at least 2*Tpoll in the future */
		uint32_t instant = conn->link.poll_interval * 2;
		if (instant < 32) {
			instant = 32;  /* Minimum 32 slots */
		}

		/* Send LMP_switch_req */
		ull_bredr_lmp_switch_req(conn, instant);

		conn->lc_state = BREDR_LC_WAIT_SWITCH_CFM;
	} else {
		/* Role switch not allowed */
		LOG_WRN("Role switch not allowed");
	}
}

void lmp_proc_rem_switch(struct ull_bredr_conn *conn)
{
	uint8_t lt_addr;
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	/* Handle remote role switch request */
	if (ull_bredr_role_switch_start(link_id, &lt_addr)) {
		/* Accept the role switch */
		ull_bredr_send_pdu_acc(link_id, LMP_SWITCH_REQ, conn->link.role);
		conn->role_switch_pending = 1;
		conn->lc_state = BREDR_LC_WAIT_SWITCH_CMP;
	} else {
		/* Reject the role switch */
		ull_bredr_send_pdu_not_acc(link_id, LMP_SWITCH_REQ,
					   BT_HCI_ERR_ROLE_CHANGE_NOT_ALLOWED,
					   conn->link.role);
	}
}

void lmp_proc_switch_cmp(struct ull_bredr_conn *conn, uint8_t status)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct net_buf *buf;
	struct bt_hci_evt_role_change *evt;

	ull_bredr_role_switch_finished(link_id, (status == 0));

	/* Send HCI Role Change event */
	buf = bt_buf_get_evt(BT_HCI_EVT_ROLE_CHANGE, false, K_NO_WAIT);
	if (buf) {
		evt = net_buf_add(buf, sizeof(*evt));
		evt->status = status;
		memcpy(evt->bdaddr.val, conn->lll.bd_addr, 6);
		evt->role = conn->lll.role;
		bt_recv(buf);
	}

	LOG_DBG("Role switch complete: handle=%u, status=%u, new_role=%u",
		conn->lll.handle, status, conn->lll.role);

	conn->lc_state = BREDR_LC_CONNECTED;
}

void lmp_proc_rsw_done(struct ull_bredr_conn *conn, uint8_t status)
{
	lmp_proc_switch_cmp(conn, status);
}

/*
 * Max Slot Management
 * Bluetooth Core Spec Vol 2, Part C - Max slot negotiation
 */
void lmp_proc_max_slot_mgt(struct ull_bredr_conn *conn, uint8_t max_slot)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct net_buf *buf;
	struct bt_hci_evt_max_slots_changed *evt;

	if (max_slot != conn->link.tx_max_slot_cur) {
		/* Send HCI Max Slot Change event */
		buf = bt_buf_get_evt(BT_HCI_EVT_MAX_SLOTS_CHANGED, false, K_NO_WAIT);
		if (buf) {
			evt = net_buf_add(buf, sizeof(*evt));
			evt->handle = sys_cpu_to_le16(link_id);
			evt->max_slots = max_slot;
			bt_recv(buf);
		}

		LOG_DBG("Max slot: %u -> %u", conn->link.tx_max_slot_cur, max_slot);
	}

	conn->link.tx_max_slot_cur = max_slot;
	ull_bredr_send_pdu_max_slot(link_id, max_slot, conn->link.role);
}

/*
 * Connection Sequence Check
 * Verifies mandatory procedures are complete
 */
bool lmp_proc_conn_seq_done(struct ull_bredr_conn *conn)
{
	/* Check if all mandatory procedures are complete */
	if (!conn->info.recv_rem_ver_rec) {
		return false;
	}

	if (!(conn->info.remote_feat_rec & BIT(0))) {
		return false;
	}

	return true;
}

/*
 * Detach/Release Functions
 */
void lmp_proc_release(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	ull_bredr_acl_disc(link_id);
}

/*
 * Timeout Functions
 */
void lmp_proc_restore_to(struct ull_bredr_conn *conn)
{
	/* Restore link supervision timeout */
	conn->lll.supervision_timeout = conn->link.link_timeout;
}


/*
 * LMP State Machine Run
 * Called during connection prepare to process pending LMP procedures.
 * This is similar to BLE's ull_cp_run() which runs:
 * - llcp_rr_run() for remote request FSM
 * - llcp_lr_run() for local request FSM
 *
 * For BR/EDR, we process pending local and remote LMP procedures.
 */
void lmp_run(struct ull_bredr_conn *conn)
{
	/* Check if connection is valid */
	if (!conn || conn->lll.handle == 0xFFFF) {
		return;
	}

	/* Process based on current LC state */
	switch (conn->lc_state) {
	case BREDR_LC_CONNECTED:
		/* Normal connected state - check for pending procedures */

		/* Check for pending local requests */
		if (conn->req.loc_vers_req) {
			/* Version exchange in progress */
			break;
		}

		if (conn->req.loc_name_req) {
			/* Name request in progress */
			break;
		}

		if (conn->req.loc_auth_req) {
			/* Authentication in progress */
			break;
		}

		if (conn->req.loc_enc_req) {
			/* Encryption in progress */
			break;
		}

		if (conn->req.loc_switch_req) {
			/* Role switch in progress */
			break;
		}

		/* Check for pending peer requests */
		if (conn->req.peer_auth_req) {
			/* Peer authentication request pending */
			break;
		}

		if (conn->req.peer_enc_req) {
			/* Peer encryption request pending */
			break;
		}

		if (conn->req.peer_switch_req) {
			/* Peer role switch request pending */
			break;
		}

		if (conn->req.peer_detach_req) {
			/* Peer detach request pending */
			break;
		}
		break;

	case BREDR_LC_WAIT_VERSION_CENTRAL:
	case BREDR_LC_WAIT_VERSION:
		/* Waiting for version response */
		break;

	case BREDR_LC_WAIT_FEATURES:
	case BREDR_LC_WAIT_EXT_FEATURES:
		/* Waiting for features response */
		break;

	case BREDR_LC_WAIT_NAME:
		/* Waiting for name response */
		break;

	case BREDR_LC_WAIT_AUTH_SRES:
	case BREDR_LC_WAIT_AU_RAND_RSP:
	case BREDR_LC_WAIT_AU_RAND_PEER:
	case BREDR_LC_WAIT_SRES_RSP:
	case BREDR_LC_WAIT_SRES_INIT:
		/* Authentication in progress */
		break;

	case BREDR_LC_WAIT_ENC_MODE_CFM:
	case BREDR_LC_WAIT_ENC_SIZE_CFM:
	case BREDR_LC_WAIT_ENC_START_CFM:
	case BREDR_LC_WAIT_ENC_STOP_CFM:
		/* Encryption in progress */
		break;

	case BREDR_LC_WAIT_SWITCH_CFM:
	case BREDR_LC_WAIT_SWITCH_CMP:
		/* Role switch in progress */
		break;

	case BREDR_LC_WAIT_DETACH_TX_CFM:
	case BREDR_LC_WAIT_DETACH_TIMEOUT:
		/* Detach in progress */
		break;

	default:
		/* Other states - no action needed */
		break;
	}

	/* Check for procedure response timeout */
	/* PRT (Procedure Response Timeout) handling
	 * Similar to BLE's llcp_prt_elapse()
	 *
	 * The LMP response timeout is handled by the k_work_delayable
	 * started in lmp_proc_start_lmp_to(). When it expires,
	 * lmp_timeout_handler() is called which handles the timeout
	 * based on the current LC state.
	 *
	 * Here we just check if we need to restart the timeout
	 * for multi-step procedures.
	 */
	if (conn->lmp_timeout_restart) {
		conn->lmp_timeout_restart = false;
		lmp_proc_start_lmp_to(conn);
	}
}

/*
 * Packet Type Table Procedure
 * Bluetooth Core Spec Vol 2, Part C - Packet type table negotiation
 */
int lmp_proc_packet_type_table(struct ull_bredr_conn *conn, uint8_t table)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	ull_bredr_send_pdu_ptt_req(link_id, table, conn->link.role);
	return 0;
}

void lmp_proc_packet_type_table_rx(struct ull_bredr_conn *conn,
				   uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_ext_header *ext_hdr = (struct pdu_lmp_ext_header *)pdu;

	if (ext_hdr->escape != LMP_ESCAPE_4) {
		return;
	}

	if (ext_hdr->ext_opcode == LMP_EXT_PACKET_TYPE_TABLE_REQ) {
		uint8_t ptt = pdu[3];  /* Packet type table value */
		uint8_t link_id = ull_bredr_conn_handle_get(conn);

		LOG_DBG("Packet type table request: ptt=%u", ptt);

		/* Accept the packet type table change */
		conn->link.cur_packet_type_table = ptt;

		/* Send LMP_accepted_ext */
		ull_bredr_send_pdu_acc_ext4(link_id, LMP_EXT_PACKET_TYPE_TABLE_REQ,
					    ext_hdr->tid);
	}
}

/*
 * Timing Accuracy Procedure
 * Bluetooth Core Spec Vol 2, Part C - Timing accuracy request
 */
int lmp_proc_timing_accuracy_request(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct pdu_lmp_header pdu;

	pdu.tid = conn->link.role;
	pdu.opcode = LMP_TIMING_ACCURACY_REQ;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

void lmp_proc_timing_accuracy_rx(struct ull_bredr_conn *conn,
				 uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	if (hdr->opcode == LMP_TIMING_ACCURACY_REQ) {
		uint8_t link_id = ull_bredr_conn_handle_get(conn);
		struct {
			struct pdu_lmp_header hdr;
			uint8_t drift;
			uint8_t jitter;
		} __packed res;

		res.hdr.tid = hdr->tid;
		res.hdr.opcode = LMP_TIMING_ACCURACY_RES;
		res.drift = 20;   /* 20 ppm drift */
		res.jitter = 10;  /* 10 us jitter */

		ull_bredr_send_lmp(link_id, &res, sizeof(res));
	}
}

/*
 * Power Control Procedure
 * Bluetooth Core Spec Vol 2, Part C - Power control
 */
int lmp_proc_power_control(struct ull_bredr_conn *conn, int8_t delta)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct {
		struct pdu_lmp_header hdr;
		int8_t power_adjustment;
	} __packed pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.opcode = LMP_POWER_CTRL_REQ;
	pdu.power_adjustment = delta;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

void lmp_proc_power_control_rx(struct ull_bredr_conn *conn,
			       uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_header *hdr = (struct pdu_lmp_header *)pdu;

	if (hdr->opcode == LMP_POWER_CTRL_REQ) {
		uint8_t link_id = ull_bredr_conn_handle_get(conn);
		struct {
			struct pdu_lmp_header hdr;
			uint8_t power_adjustment;
		} __packed res;

		res.hdr.tid = hdr->tid;
		res.hdr.opcode = LMP_POWER_CTRL_RES;
		res.power_adjustment = 0;  /* No adjustment */

		ull_bredr_send_lmp(link_id, &res, sizeof(res));
	}
}

/*
 * Sniff Subrating Procedure
 * Bluetooth Core Spec Vol 2, Part C - Sniff subrating
 */
int lmp_proc_sniff_subrating(struct ull_bredr_conn *conn,
			     uint16_t max_latency, uint16_t min_remote_timeout,
			     uint16_t min_local_timeout)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct {
		struct pdu_lmp_ext_header hdr;
		uint8_t max_sniff_subrate;
		uint16_t min_sniff_mode_timeout;
		uint16_t sniff_subrating_instant;
	} __packed pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.escape = LMP_ESCAPE_4;
	pdu.hdr.ext_opcode = LMP_EXT_SNIFF_SUBRATING_REQ;
	pdu.max_sniff_subrate = (uint8_t)(max_latency / conn->sniff_interval);
	pdu.min_sniff_mode_timeout = sys_cpu_to_le16(min_remote_timeout);
	pdu.sniff_subrating_instant = 0;

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

void lmp_proc_sniff_subrating_rx(struct ull_bredr_conn *conn,
				 uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_ext_header *ext_hdr = (struct pdu_lmp_ext_header *)pdu;

	if (ext_hdr->escape != LMP_ESCAPE_4) {
		return;
	}

	if (ext_hdr->ext_opcode == LMP_EXT_SNIFF_SUBRATING_REQ ||
	    ext_hdr->ext_opcode == LMP_EXT_SNIFF_SUBRATING_RES) {
		uint8_t link_id = ull_bredr_conn_handle_get(conn);

		LOG_DBG("Sniff subrating received");

		if (ext_hdr->ext_opcode == LMP_EXT_SNIFF_SUBRATING_REQ) {
			/* Send response */
			struct {
				struct pdu_lmp_ext_header hdr;
				uint8_t max_sniff_subrate;
				uint16_t min_sniff_mode_timeout;
				uint16_t sniff_subrating_instant;
			} __packed res;

			res.hdr.tid = ext_hdr->tid;
			res.hdr.escape = LMP_ESCAPE_4;
			res.hdr.ext_opcode = LMP_EXT_SNIFF_SUBRATING_RES;
			res.max_sniff_subrate = 1;
			res.min_sniff_mode_timeout = 0;
			res.sniff_subrating_instant = 0;

			ull_bredr_send_lmp(link_id, &res, sizeof(res));
		}
	}
}

/*
 * Channel Classification Procedure
 * Bluetooth Core Spec Vol 2, Part C - Channel classification
 */
int lmp_proc_channel_classification(struct ull_bredr_conn *conn, uint8_t *map)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	struct {
		struct pdu_lmp_ext_header hdr;
		uint8_t afh_channel_classification[10];
	} __packed pdu;

	pdu.hdr.tid = conn->link.role;
	pdu.hdr.escape = LMP_ESCAPE_4;
	pdu.hdr.ext_opcode = LMP_EXT_CHANNEL_CLASSIFICATION_REQ;
	memcpy(pdu.afh_channel_classification, map, 10);

	ull_bredr_send_lmp(link_id, &pdu, sizeof(pdu));
	return 0;
}

void lmp_proc_channel_classification_rx(struct ull_bredr_conn *conn,
					uint8_t *pdu, uint8_t len)
{
	struct pdu_lmp_ext_header *ext_hdr = (struct pdu_lmp_ext_header *)pdu;

	if (ext_hdr->escape != LMP_ESCAPE_4) {
		return;
	}

	if (ext_hdr->ext_opcode == LMP_EXT_CHANNEL_CLASSIFICATION_REQ) {
		uint8_t link_id = ull_bredr_conn_handle_get(conn);

		LOG_DBG("Channel classification received");

		/* Store the channel classification */
		memcpy(conn->afh.ch_class, &pdu[3], 10);

		/* Send LMP_accepted_ext */
		ull_bredr_send_pdu_acc_ext4(link_id, LMP_EXT_CHANNEL_CLASSIFICATION_REQ,
					    ext_hdr->tid);
	}
}

/*
 * Additional LMP Procedure Functions
 * Bluetooth Core Spec Vol 2, Part C
 */

void lmp_proc_comb_key_svr(struct ull_bredr_conn *conn, uint8_t *key)
{
	/* Store combination key from server */
	memcpy(conn->pairing_comb_key, key, 16);
	LOG_DBG("Combination key received from server");
}

void lmp_proc_con_cmp(struct ull_bredr_conn *conn)
{
	/* Connection complete - send event to host */
	lmp_proc_con_cmp_evt_send(conn, 0);
}

void lmp_proc_con_cmp_evt_send(struct ull_bredr_conn *conn, uint8_t status)
{
	struct net_buf *buf;
	struct bt_hci_evt_conn_complete *evt;

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
	evt->encr_enabled = conn->enc.enc_mode != ENC_DISABLED;

	bt_recv(buf);

	LOG_DBG("Connection complete event: handle=%u, status=%u",
		conn->lll.handle, status);
}

void lmp_proc_enc_key_refresh(struct ull_bredr_conn *conn)
{
	/* Refresh encryption key */
	conn->req.loc_enc_key_refresh = true;
	lmp_proc_initiator_epr(conn);
}

void lmp_proc_end_chk_colli(struct ull_bredr_conn *conn, uint8_t enc_mode)
{
	/* End collision check for encryption */
	conn->enc.enc_mode = enc_mode;
	LOG_DBG("Encryption collision check ended: mode=%u", enc_mode);
}

void lmp_proc_key_exch_end(struct ull_bredr_conn *conn, uint8_t reason)
{
	/* Key exchange ended */
	conn->req.loc_key_exchange_req = false;
	if (reason == 0) {
		conn->enc.key_status = KEY_PRESENT;
	}
	LOG_DBG("Key exchange ended: reason=%u", reason);
}

void lmp_proc_start_key_exch(struct ull_bredr_conn *conn)
{
	/* Start key exchange procedure */
	conn->req.loc_key_exchange_req = true;
	lmp_proc_pairing_initiate(conn);
}

void lmp_proc_legacy_pair(struct ull_bredr_conn *conn)
{
	/* Start legacy pairing */
	lmp_proc_pairing_initiate(conn);
}

void lmp_proc_pairing_cont(struct ull_bredr_conn *conn)
{
	/* Continue pairing procedure */
	LOG_DBG("Pairing continue");
}

void lmp_proc_rsw_non_epr_back(struct ull_bredr_conn *conn)
{
	/* Role switch without EPR - rollback */
	LOG_DBG("Role switch non-EPR rollback");
}

void lmp_proc_mutual_auth_end(struct ull_bredr_conn *conn, uint8_t reason)
{
	/* Mutual authentication ended */
	lmp_proc_auth_cmp(conn, reason);
}

void lmp_proc_mutual_auth_end2(struct ull_bredr_conn *conn, uint8_t reason)
{
	/* Second phase of mutual authentication ended */
	lmp_proc_auth_cmp(conn, reason);
}

/*
 * Master Key Functions
 */
void lmp_proc_mst_key(struct ull_bredr_conn *conn)
{
	/* Master key procedure */
	LOG_DBG("Master key procedure");
}

void lmp_proc_mst_send_mst_key(struct ull_bredr_conn *conn)
{
	/* Send master key */
	LOG_DBG("Send master key");
}

void lmp_proc_mst_qos_done(struct ull_bredr_conn *conn)
{
	/* Master QoS done */
	LOG_DBG("Master QoS done");
}

/*
 * Passkey Functions
 */
void lmp_proc_passkey_comm(struct ull_bredr_conn *conn)
{
	/* Passkey commitment */
	LOG_DBG("Passkey commitment");
}

void lmp_proc_start_passkey(struct ull_bredr_conn *conn)
{
	/* Start passkey entry */
	conn->ssp_state = LMP_SSP_STATE_WAIT_CONFIRM;
	LOG_DBG("Start passkey entry");
}

void lmp_proc_init_passkey_loop(struct ull_bredr_conn *conn)
{
	/* Initialize passkey loop */
	conn->sp.passkey_bit = 0;
	lmp_proc_start_passkey_loop(conn);
}

/*
 * OOB Functions
 */
void lmp_proc_start_oob(struct ull_bredr_conn *conn)
{
	/* Start OOB authentication */
	conn->ssp_state = LMP_SSP_STATE_WAIT_CONFIRM;
	LOG_DBG("Start OOB authentication");
}

void lmp_proc_skip_hl_oob_req(struct ull_bredr_conn *conn)
{
	/* Skip host-level OOB request */
	LOG_DBG("Skip HL OOB request");
}

void lmp_proc_resp_oob_wait_nonce(struct ull_bredr_conn *conn)
{
	/* Responder OOB wait for nonce */
	LOG_DBG("Responder OOB wait nonce");
}

void lmp_proc_resp_oob_nonce(struct ull_bredr_conn *conn)
{
	/* Responder OOB nonce */
	LOG_DBG("Responder OOB nonce");
}

/*
 * Packet Type Table Functions
 */
void lmp_proc_ptt(struct ull_bredr_conn *conn)
{
	/* Packet type table procedure */
	LOG_DBG("Packet type table procedure");
}

void lmp_proc_ptt_cmp(struct ull_bredr_conn *conn)
{
	/* Packet type table complete */
	LOG_DBG("Packet type table complete");
}

/*
 * Remote Name Functions
 */
void lmp_proc_rd_rem_name(struct ull_bredr_conn *conn)
{
	/* Read remote name */
	lmp_proc_name_request(conn);
}

void lmp_proc_rem_name_cont(struct ull_bredr_conn *conn)
{
	/* Remote name continue */
	LOG_DBG("Remote name continue");
}

/*
 * Remote Encryption Functions
 */
void lmp_proc_rem_enc(struct ull_bredr_conn *conn)
{
	/* Remote encryption */
	LOG_DBG("Remote encryption");
}

/*
 * Responder Functions
 */
void lmp_proc_resp_sec_auth(struct ull_bredr_conn *conn)
{
	/* Responder secure authentication */
	LOG_DBG("Responder secure authentication");
}

void lmp_proc_resp_auth(struct ull_bredr_conn *conn)
{
	/* Responder authentication */
	LOG_DBG("Responder authentication");
}

void lmp_proc_resp_calc_f3(struct ull_bredr_conn *conn)
{
	/* Responder calculate f3 */
	LOG_DBG("Responder calculate f3");
}

void lmp_proc_resp_num_comp(struct ull_bredr_conn *conn)
{
	/* Responder numeric comparison */
	LOG_DBG("Responder numeric comparison");
}

void lmp_proc_resp_pair(struct ull_bredr_conn *conn)
{
	/* Responder pairing */
	LOG_DBG("Responder pairing");
}

/*
 * Restart Encryption Functions
 */
void lmp_proc_restart_enc(struct ull_bredr_conn *conn)
{
	/* Restart encryption */
	lmp_proc_start_enc(conn);
}

void lmp_proc_restart_enc_cont(struct ull_bredr_conn *conn, uint8_t reason)
{
	/* Restart encryption continue */
	if (reason == 0) {
		lmp_proc_start_enc(conn);
	} else {
		lmp_proc_enc_cmp(conn, reason);
	}
}


/*
 * Stop Encryption Functions
 * Bluetooth Core Spec Vol 2, Part C - Encryption stop procedure
 */
void lmp_proc_stop_enc(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	/* Send LMP_stop_encryption_req */
	ull_bredr_lmp_stop_encryption_req(conn);

	/* Update state */
	conn->lc_state = BREDR_LC_WAIT_ENC_STOP_CFM;

	/* Start LMP timeout */
	lmp_proc_start_lmp_to(conn);

	LOG_DBG("Stop encryption initiated: handle=%u", conn->lll.handle);
}

void lmp_proc_send_enc_mode(struct ull_bredr_conn *conn)
{
	uint8_t link_id = ull_bredr_conn_handle_get(conn);

	/* Send LMP_encryption_mode_req */
	ull_bredr_lmp_encryption_mode_req(conn, conn->enc.new_enc_mode);

	/* Update state */
	conn->lc_state = BREDR_LC_WAIT_ENC_MODE_CFM;

	/* Start LMP timeout */
	lmp_proc_start_lmp_to(conn);

	LOG_DBG("Send encryption mode: handle=%u, mode=%u",
		conn->lll.handle, conn->enc.new_enc_mode);
}

/*
 * Semi-Permanent Key Functions
 * Bluetooth Core Spec Vol 2, Part H - Semi-permanent key handling
 */
void lmp_proc_semi_key_cmp(struct ull_bredr_conn *conn, uint8_t reason)
{
	if (reason == 0) {
		/* Semi-permanent key exchange successful */
		memcpy(conn->enc.semi_permanent_key, conn->enc.lt_key, 16);
		conn->enc.key_status = KEY_PRESENT;
		LOG_DBG("Semi-permanent key stored");
	} else {
		/* Semi-permanent key exchange failed */
		conn->enc.key_status = KEY_ABSENT;
		LOG_WRN("Semi-permanent key exchange failed: reason=%u", reason);
	}

	/* Continue with connection setup or encryption */
	if (conn->req.loc_enc_req) {
		lmp_proc_start_enc(conn);
	} else {
		conn->lc_state = BREDR_LC_CONNECTED;
	}
}

/*
 * DHKey Functions
 * Bluetooth Core Spec Vol 2, Part H - Secure Simple Pairing DHKey computation
 */
void lmp_proc_dhkey(struct ull_bredr_conn *conn)
{
	/* Compute DHKey using P-256 ECDH
	 * DHKey = P256(local_private_key, remote_public_key)
	 *
	 * This is a placeholder - real implementation requires
	 * ECC P-256 computation as specified in Core Spec Vol 2, Part H
	 */
	lmp_proc_ssp_compute_dhkey(conn->sp.loc_rand_n,  /* Using as private key placeholder */
				   conn->sp.rem_rand_n,  /* Using as public key placeholder */
				   conn->sp.dhkey);

	LOG_DBG("DHKey computed");

	/* Continue with link key calculation */
	lmp_proc_calc_link_key(conn);
}

void lmp_proc_init_calc_f3(struct ull_bredr_conn *conn)
{
	uint8_t dhkey_check[16];
	uint8_t io_cap[3];

	/* Calculate f3 (DHKey check value) as initiator
	 * DHKey_check = f3(DHKey, Na, Nb, r, IOcap_A, A, IOcap_B, B)
	 *
	 * For initiator:
	 * - Na = local nonce
	 * - Nb = remote nonce
	 * - A = local BD_ADDR
	 * - B = remote BD_ADDR
	 */
	io_cap[0] = conn->sp.io_cap_loc[0];
	io_cap[1] = conn->sp.io_cap_loc[1];
	io_cap[2] = conn->sp.io_cap_loc[2];

	lmp_proc_ssp_compute_dhkey_check(conn->sp.dhkey,
					 conn->sp.nonce_loc,
					 conn->info.local_bd_addr,
					 io_cap,
					 dhkey_check);

	/* Store local DHKey check */
	memcpy(conn->sp.dhkey_check, dhkey_check, 16);

	/* Send LMP_DHKey_Check */
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	ull_bredr_send_pdu_dhkey_chk(link_id, dhkey_check, conn->link.role);

	/* Update state */
	conn->lc_state = BREDR_LC_WAIT_DHKEY_CHECK_PEER;

	LOG_DBG("Initiator f3 calculated and DHKey check sent");
}

/*
 * Packet Type Functions
 * Bluetooth Core Spec Vol 2, Part C - Packet type management
 */
void lmp_proc_packet_type(struct ull_bredr_conn *conn, int idx)
{
	/* Update packet type based on index */
	uint16_t packet_type = conn->link.acl_packet_type;

	/* Check remote features for packet type support */
	if (!lmp_proc_get_feature(conn->info.remote_features[0],
				  FEAT_3_SLOT_BIT_POS)) {
		/* Remove 3-slot packets */
		packet_type &= ~(0x0C00);  /* DM3, DH3 */
	}

	if (!lmp_proc_get_feature(conn->info.remote_features[0],
				  FEAT_5_SLOT_BIT_POS)) {
		/* Remove 5-slot packets */
		packet_type &= ~(0xC000);  /* DM5, DH5 */
	}

	/* Check for EDR support */
	if (!lmp_proc_get_feature(conn->info.remote_features[0],
				  FEAT_EDR_ACL_2MBPS_BIT_POS)) {
		/* Remove 2Mbps EDR packets */
		packet_type &= ~(0x0002);  /* 2-DH1 */
	}

	if (!lmp_proc_get_feature(conn->info.remote_features[0],
				  FEAT_EDR_ACL_3MBPS_BIT_POS)) {
		/* Remove 3Mbps EDR packets */
		packet_type &= ~(0x0004);  /* 3-DH1 */
	}

	conn->link.cur_packet_type = packet_type;

	LOG_DBG("Packet type updated: 0x%04x", packet_type);
}

/*
 * Local Authentication Functions
 * Bluetooth Core Spec Vol 2, Part C - Local authentication initiation
 */
void lmp_proc_loc_auth(struct ull_bredr_conn *conn)
{
	/* Start local authentication procedure */
	conn->req.loc_auth_req = true;
	conn->link.initiator = true;

	/* Check if SSP is supported */
	if (lmp_proc_get_feature(conn->info.remote_features[0], FEAT_SSP_BIT_POS) &&
	    ull_bredr_get_sp_en()) {
		/* Use Secure Simple Pairing */
		lmp_proc_ssp_initiate(conn);
	} else {
		/* Use legacy authentication */
		lmp_proc_auth_initiate(conn);
	}

	/* Start LMP timeout */
	lmp_proc_start_lmp_to(conn);

	LOG_DBG("Local authentication initiated: handle=%u", conn->lll.handle);
}

/*
 * Extended Features Functions
 * Bluetooth Core Spec Vol 2, Part C - Extended features exchange
 */
void lmp_proc_ext_feat(struct ull_bredr_conn *conn, uint8_t page_nb)
{
	/* Request extended features page */
	lmp_proc_ext_features_exchange(conn, page_nb);

	/* Update state */
	conn->lc_state = BREDR_LC_WAIT_EXT_FEATURES;

	/* Start LMP timeout */
	lmp_proc_start_lmp_to(conn);

	LOG_DBG("Extended features request: page=%u", page_nb);
}

void lmp_proc_feat(struct ull_bredr_conn *conn)
{
	/* Request basic features (page 0) */
	lmp_proc_features_exchange(conn);

	/* Update state */
	conn->lc_state = BREDR_LC_WAIT_FEATURES;

	/* Start LMP timeout */
	lmp_proc_start_lmp_to(conn);

	LOG_DBG("Features request initiated");
}

/*
 * Host Connection Functions
 * Bluetooth Core Spec Vol 2, Part C - Host connection request
 */
void lmp_proc_hl_connect(struct ull_bredr_conn *conn)
{
	/* Send LMP_host_connection_req to notify remote host */
	ull_bredr_lmp_host_conn_req(conn);

	/* Update state */
	conn->lc_state = BREDR_LC_WAIT_HOST_CONN;

	/* Start LMP timeout */
	lmp_proc_start_lmp_to(conn);

	LOG_DBG("Host connection request sent: handle=%u", conn->lll.handle);
}

/*
 * Version Functions
 * Bluetooth Core Spec Vol 2, Part C - Version exchange
 */
void lmp_proc_version(struct ull_bredr_conn *conn)
{
	/* Request version information */
	lmp_proc_version_exchange(conn);

	/* Update state */
	conn->lc_state = BREDR_LC_WAIT_VERSION;

	/* Start LMP timeout */
	lmp_proc_start_lmp_to(conn);

	LOG_DBG("Version request initiated");
}

/*
 * Secure Authentication Functions
 * Bluetooth Core Spec Vol 2, Part H - Secure authentication SRES computation
 */
void lmp_proc_sec_auth_compute_sres(struct ull_bredr_conn *conn)
{
	uint8_t sres[4];
	uint8_t aco[12];

	/* Compute SRES using E1 algorithm
	 * SRES = E1(link_key, AU_RAND, BD_ADDR)
	 *
	 * The E1 algorithm uses SAFER+ block cipher
	 * This is a placeholder - real implementation requires
	 * SAFER+ as specified in Core Spec Vol 2, Part H
	 */
	lmp_proc_auth_compute_sres(conn->link_key,
				   conn->auth_au_rand,
				   conn->lll.bd_addr,
				   sres,
				   aco);

	/* Store computed values */
	memcpy(conn->enc.sres, sres, 4);
	memcpy(conn->auth_aco, aco, 12);

	/* Send SRES response */
	uint8_t link_id = ull_bredr_conn_handle_get(conn);
	ull_bredr_send_pdu_sres(link_id, sres, conn->link.role);

	LOG_DBG("Secure auth SRES computed and sent");
}

/*
 * Encryption Key Size Functions
 * Bluetooth Core Spec Vol 2, Part C - Encryption key size negotiation
 */
void lmp_proc_start_enc_key_size(struct ull_bredr_conn *conn)
{
	uint8_t key_size;

	/* Determine encryption key size
	 * Start with maximum supported key size (16 bytes)
	 * and negotiate down if needed
	 */
	key_size = 16;

	/* Check key size mask from remote */
	if (conn->enc.key_size_mask != 0) {
		/* Find highest supported key size */
		for (int i = 16; i >= 7; i--) {
			if (conn->enc.key_size_mask & BIT(i - 1)) {
				key_size = i;
				break;
			}
		}
	}

	conn->enc.enc_size = key_size;

	/* Send LMP_encryption_key_size_req */
	ull_bredr_lmp_encryption_key_size_req(conn, key_size);

	/* Update state */
	conn->lc_state = BREDR_LC_WAIT_ENC_SIZE_CFM;

	/* Start LMP timeout */
	lmp_proc_start_lmp_to(conn);

	LOG_DBG("Encryption key size negotiation: size=%u", key_size);
}

/*
 * AFH Start/Stop Functions
 * Bluetooth Core Spec Vol 2, Part B - AFH management
 */
void lmp_proc_afh_start(struct ull_bredr_conn *conn)
{
	/* Start AFH for this connection */
	ull_bredr_afh_set(conn, true);
	conn->afh.en = true;

	LOG_DBG("AFH started: handle=%u", conn->lll.handle);
}

void lmp_proc_afh_stop_reporting(struct ull_bredr_conn *conn)
{
	/* Stop AFH channel classification reporting */
	conn->afh.reporting_en = false;

	LOG_DBG("AFH reporting stopped: handle=%u", conn->lll.handle);
}

void lmp_proc_afh_restore_reporting(struct ull_bredr_conn *conn)
{
	/* Restore AFH channel classification reporting */
	conn->afh.reporting_en = true;

	LOG_DBG("AFH reporting restored: handle=%u", conn->lll.handle);
}
