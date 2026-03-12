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
 * BR/EDR LMP Procedures
 * Bluetooth Core Spec Vol 2, Part C: Link Manager Protocol
 */

#ifndef SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_LMP_PROC_H_
#define SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_LMP_PROC_H_

#include <zephyr/types.h>
#include "ull_bredr.h"

/*
 * LMP Procedure States
 */
enum lmp_proc_state {
	LMP_PROC_STATE_IDLE = 0,
	LMP_PROC_STATE_PENDING,
	LMP_PROC_STATE_WAIT_RESPONSE,
	LMP_PROC_STATE_COMPLETE,
	LMP_PROC_STATE_ERROR,
};

/*
 * LMP Transaction IDs
 */
#define LMP_TID_INITIATOR    0
#define LMP_TID_RESPONDER    1

/*
 * Local Transaction Collision States
 * Bluetooth Core Spec Vol 2, Part C - Transaction collision handling
 */
#define BREDR_TRANS_UNUSED     0
#define BREDR_TRANS_INUSE      1

/*
 * Key Status
 */
#define KEY_ABSENT           0
#define KEY_PRESENT          1

/*
 * Encryption Modes
 */
#define ENC_DISABLED         0
#define ENC_POINT_TO_POINT   1
#define ENC_BROADCAST        2

/*
 * Authentication States
 */
enum lmp_auth_state {
	LMP_AUTH_STATE_IDLE = 0,
	LMP_AUTH_STATE_WAIT_AU_RAND,
	LMP_AUTH_STATE_WAIT_SRES,
	LMP_AUTH_STATE_WAIT_IN_RAND,
	LMP_AUTH_STATE_WAIT_COMB_KEY,
	LMP_AUTH_STATE_COMPLETE,
	LMP_AUTH_STATE_FAILED,
};

/*
 * Encryption States
 */
enum lmp_enc_state {
	LMP_ENC_STATE_IDLE = 0,
	LMP_ENC_STATE_WAIT_MODE_REQ,
	LMP_ENC_STATE_WAIT_KEY_SIZE,
	LMP_ENC_STATE_WAIT_START_ENC,
	LMP_ENC_STATE_WAIT_STOP_ENC,
	LMP_ENC_STATE_ENCRYPTED,
	LMP_ENC_STATE_FAILED,
};

/*
 * SSP States
 */
enum lmp_ssp_state {
	LMP_SSP_STATE_IDLE = 0,
	LMP_SSP_STATE_WAIT_IO_CAP,
	LMP_SSP_STATE_WAIT_PUBLIC_KEY,
	LMP_SSP_STATE_WAIT_CONFIRM,
	LMP_SSP_STATE_WAIT_NUMBER,
	LMP_SSP_STATE_WAIT_DHKEY_CHECK,
	LMP_SSP_STATE_COMPLETE,
	LMP_SSP_STATE_FAILED,
};

/*
 * SSP Association Models
 */
enum lmp_ssp_model {
	LMP_SSP_MODEL_NUMERIC_COMPARISON = 0,
	LMP_SSP_MODEL_JUST_WORKS,
	LMP_SSP_MODEL_PASSKEY_ENTRY,
	LMP_SSP_MODEL_OOB,
};

/*
 * IO Capabilities
 */
#define LMP_IO_CAP_DISPLAY_ONLY       0x00
#define LMP_IO_CAP_DISPLAY_YES_NO     0x01
#define LMP_IO_CAP_KEYBOARD_ONLY      0x02
#define LMP_IO_CAP_NO_INPUT_NO_OUTPUT 0x03

/*
 * OOB Data Present
 */
#define LMP_OOB_NOT_PRESENT           0x00
#define LMP_OOB_P192_PRESENT          0x01
#define LMP_OOB_P256_PRESENT          0x02
#define LMP_OOB_P192_P256_PRESENT     0x03

/*
 * Authentication Requirements
 */
#define LMP_AUTH_REQ_NO_MITM_NO_BONDING       0x00
#define LMP_AUTH_REQ_MITM_NO_BONDING          0x01
#define LMP_AUTH_REQ_NO_MITM_DEDICATED_BONDING 0x02
#define LMP_AUTH_REQ_MITM_DEDICATED_BONDING   0x03
#define LMP_AUTH_REQ_NO_MITM_GENERAL_BONDING  0x04
#define LMP_AUTH_REQ_MITM_GENERAL_BONDING     0x05

/*
 * Link Key Types
 */
#define LMP_KEY_TYPE_COMBINATION              0x00
#define LMP_KEY_TYPE_DEBUG_COMBINATION        0x03
#define LMP_KEY_TYPE_UNAUTHENTICATED_P192     0x04
#define LMP_KEY_TYPE_AUTHENTICATED_P192       0x05
#define LMP_KEY_TYPE_CHANGED_COMBINATION      0x06
#define LMP_KEY_TYPE_UNAUTHENTICATED_P256     0x07
#define LMP_KEY_TYPE_AUTHENTICATED_P256       0x08

/*
 * Feature Bit Positions (commonly used)
 */
#define FEAT_3_SLOT_BIT_POS           0
#define FEAT_5_SLOT_BIT_POS           1
#define FEAT_ENCRYPTION_BIT_POS       2
#define FEAT_SLOT_OFFSET_BIT_POS      3
#define FEAT_TIMING_ACCURACY_BIT_POS  4
#define FEAT_ROLE_SWITCH_BIT_POS      5
#define FEAT_HOLD_MODE_BIT_POS        6
#define FEAT_SNIFF_MODE_BIT_POS       7
#define FEAT_PARK_STATE_BIT_POS       8
#define FEAT_POWER_CTRL_REQ_BIT_POS   9
#define FEAT_CQDDR_BIT_POS            10
#define FEAT_SCO_LINK_BIT_POS         11
#define FEAT_HV2_PACKETS_BIT_POS      12
#define FEAT_HV3_PACKETS_BIT_POS      13
#define FEAT_U_LAW_LOG_BIT_POS        14
#define FEAT_A_LAW_LOG_BIT_POS        15
#define FEAT_CVSD_BIT_POS             16
#define FEAT_PAGING_PARAM_NEG_BIT_POS 17
#define FEAT_POWER_CTRL_BIT_POS       18
#define FEAT_TRANSPARENT_SCO_BIT_POS  19
#define FEAT_FLOW_CTRL_LAG_BIT_POS    20
#define FEAT_BROADCAST_ENC_BIT_POS    23
#define FEAT_EDR_ACL_2MBPS_BIT_POS    25
#define FEAT_EDR_ACL_3MBPS_BIT_POS    26
#define FEAT_ENH_INQ_SCAN_BIT_POS     27
#define FEAT_INTERLACED_INQ_SCAN_BIT_POS 28
#define FEAT_INTERLACED_PAGE_SCAN_BIT_POS 29
#define FEAT_RSSI_INQ_RES_BIT_POS     30
#define FEAT_EV3_PACKETS_BIT_POS      31
#define FEAT_EV4_PACKETS_BIT_POS      32
#define FEAT_EV5_PACKETS_BIT_POS      33
#define FEAT_AFH_CAP_S_BIT_POS        35
#define FEAT_AFH_CLASS_S_BIT_POS      36
#define FEAT_BR_EDR_NOT_SUPP_BIT_POS  37
#define FEAT_LE_SUPP_BIT_POS          38
#define FEAT_3_SLOT_EDR_ACL_BIT_POS   39
#define FEAT_5_SLOT_EDR_ACL_BIT_POS   40
#define FEAT_SNIFF_SUBRATING_BIT_POS  41
#define FEAT_PAUSE_ENCRYPT_BIT_POS    42
#define FEAT_AFH_CAP_M_BIT_POS        43
#define FEAT_AFH_CLASS_M_BIT_POS      44
#define FEAT_EDR_ESCO_2MBPS_BIT_POS   45
#define FEAT_EDR_ESCO_3MBPS_BIT_POS   46
#define FEAT_3_SLOT_EDR_ESCO_BIT_POS  47
#define FEAT_EIR_BIT_POS              48
#define FEAT_SIMUL_LE_BREDR_BIT_POS   49
#define FEAT_SSP_BIT_POS              51
#define FEAT_ENCAPS_PDU_BIT_POS       52
#define FEAT_ERR_DATA_REP_BIT_POS     53
#define FEAT_NON_FLUSH_PBF_BIT_POS    54
#define FEAT_LSTO_CHG_EVT_BIT_POS     56
#define FEAT_INQ_TX_PWR_LVL_BIT_POS   57
#define FEAT_EPC_BIT_POS              58
#define FEAT_EXT_FEATURES_BIT_POS     63

/*
 * LMP Procedure Simple Context
 * Note: The full lmp_proc_ctx is defined in lmp_internal.h
 */
struct lmp_proc_simple_ctx {
	uint8_t state;
	uint8_t tid;
	uint8_t opcode;
	uint8_t error_code;
	uint8_t retries;
};

/*
 * Authentication Context
 */
struct lmp_auth_ctx {
	uint8_t state;
	uint8_t initiator;
	uint8_t au_rand[16];
	uint8_t sres[4];
	uint8_t aco[12];
	uint8_t in_rand[16];
	uint8_t comb_key[16];
};

/*
 * Encryption Context
 */
struct lmp_enc_ctx {
	uint8_t state;
	uint8_t mode;
	uint8_t key_size;
	uint8_t en_rand[16];
	uint8_t encryption_key[16];
};

/*
 * SSP Context
 */
struct lmp_ssp_ctx {
	uint8_t state;
	uint8_t initiator;
	uint8_t association_model;
	uint8_t local_io_cap;
	uint8_t remote_io_cap;
	uint8_t local_oob;
	uint8_t remote_oob;
	uint8_t local_auth_req;
	uint8_t remote_auth_req;
	uint8_t local_public_key[64];
	uint8_t remote_public_key[64];
	uint8_t local_nonce[16];
	uint8_t remote_nonce[16];
	uint8_t local_commitment[16];
	uint8_t remote_commitment[16];
	uint8_t dhkey[32];
	uint8_t local_dhkey_check[16];
	uint8_t remote_dhkey_check[16];
	uint32_t passkey;
	uint8_t passkey_bit;
};

/*
 * LMP Procedure Functions
 */

/* Connection Setup */
int lmp_proc_connection_setup(struct ull_bredr_conn *conn);
int lmp_proc_connection_complete(struct ull_bredr_conn *conn);

/* Version Exchange */
int lmp_proc_version_exchange(struct ull_bredr_conn *conn);
void lmp_proc_version_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);

/* Features Exchange */
int lmp_proc_features_exchange(struct ull_bredr_conn *conn);
int lmp_proc_ext_features_exchange(struct ull_bredr_conn *conn, uint8_t page);
void lmp_proc_features_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);

/* Name Request */
int lmp_proc_name_request(struct ull_bredr_conn *conn);
void lmp_proc_name_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);

/* Clock Offset */
int lmp_proc_clock_offset_request(struct ull_bredr_conn *conn);
void lmp_proc_clock_offset_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);

/* Authentication */
int lmp_proc_auth_initiate(struct ull_bredr_conn *conn);
int lmp_proc_auth_respond(struct ull_bredr_conn *conn, uint8_t *au_rand);
void lmp_proc_auth_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);
int lmp_proc_auth_compute_sres(uint8_t *link_key, uint8_t *au_rand,
			       uint8_t *bd_addr, uint8_t *sres, uint8_t *aco);

/* Pairing (Legacy) */
int lmp_proc_pairing_initiate(struct ull_bredr_conn *conn);
void lmp_proc_pairing_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);
int lmp_proc_pairing_compute_init_key(uint8_t *pin, uint8_t pin_len,
				      uint8_t *bd_addr, uint8_t *in_rand,
				      uint8_t *init_key);
int lmp_proc_pairing_compute_comb_key(uint8_t *init_key, uint8_t *comb_rand,
				      uint8_t *comb_key);

/* Encryption */
int lmp_proc_encryption_start(struct ull_bredr_conn *conn);
int lmp_proc_encryption_stop(struct ull_bredr_conn *conn);
int lmp_proc_encryption_pause(struct ull_bredr_conn *conn);
int lmp_proc_encryption_resume(struct ull_bredr_conn *conn);
void lmp_proc_encryption_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);
int lmp_proc_encryption_compute_key(uint8_t *link_key, uint8_t *en_rand,
				    uint8_t *aco, uint8_t key_size,
				    uint8_t *encryption_key);

/* Secure Simple Pairing */
int lmp_proc_ssp_initiate(struct ull_bredr_conn *conn);
int lmp_proc_ssp_respond(struct ull_bredr_conn *conn);
void lmp_proc_ssp_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);
int lmp_proc_ssp_determine_model(uint8_t local_io, uint8_t remote_io,
				 uint8_t local_oob, uint8_t remote_oob,
				 uint8_t local_auth, uint8_t remote_auth);
int lmp_proc_ssp_compute_confirm(uint8_t *public_key_x, uint8_t *public_key_y,
				 uint8_t *nonce, uint8_t z, uint8_t *confirm);
int lmp_proc_ssp_compute_dhkey(uint8_t *local_private_key,
			       uint8_t *remote_public_key, uint8_t *dhkey);
int lmp_proc_ssp_compute_link_key(uint8_t *dhkey, uint8_t *nonce_a,
				  uint8_t *nonce_b, uint8_t *bd_addr_a,
				  uint8_t *bd_addr_b, uint8_t *link_key);
int lmp_proc_ssp_compute_dhkey_check(uint8_t *dhkey, uint8_t *nonce,
				     uint8_t *bd_addr, uint8_t *io_cap,
				     uint8_t *check);

/* User Confirmation */
int lmp_proc_user_confirm_reply(struct ull_bredr_conn *conn, uint8_t accept);
int lmp_proc_user_passkey_reply(struct ull_bredr_conn *conn, uint32_t passkey);
int lmp_proc_user_passkey_negative_reply(struct ull_bredr_conn *conn);

/* Power Modes */
int lmp_proc_hold_mode(struct ull_bredr_conn *conn, uint16_t hold_time);
int lmp_proc_sniff_mode(struct ull_bredr_conn *conn, uint16_t interval,
			uint16_t attempt, uint16_t timeout);
int lmp_proc_exit_sniff_mode(struct ull_bredr_conn *conn);
int lmp_proc_park_mode(struct ull_bredr_conn *conn);
int lmp_proc_exit_park_mode(struct ull_bredr_conn *conn);
void lmp_proc_power_mode_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);

/* Role Switch */
int lmp_proc_role_switch(struct ull_bredr_conn *conn);
void lmp_proc_role_switch_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);

/* QoS */
int lmp_proc_qos_setup(struct ull_bredr_conn *conn, uint16_t poll_interval);
void lmp_proc_qos_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);

/* AFH */
int lmp_proc_afh_set_channel_map(struct ull_bredr_conn *conn, uint8_t *map);
void lmp_proc_afh_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);
void lmp_proc_afh_start(struct ull_bredr_conn *conn);
void lmp_proc_afh_stop_reporting(struct ull_bredr_conn *conn);
void lmp_proc_afh_restore_reporting(struct ull_bredr_conn *conn);

/* SCO */
int lmp_proc_sco_setup(struct ull_bredr_conn *conn, uint8_t sco_handle,
		       uint8_t d_sco, uint8_t t_sco, uint8_t packet_type,
		       uint8_t air_mode);
int lmp_proc_sco_remove(struct ull_bredr_conn *conn, uint8_t sco_handle,
			uint8_t reason);
void lmp_proc_sco_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);

/* eSCO */
int lmp_proc_esco_setup(struct ull_bredr_conn *conn,
			struct pdu_lmp_esco_link_req *params);
int lmp_proc_esco_modify(struct ull_bredr_conn *conn,
			 struct pdu_lmp_esco_link_req *params);
int lmp_proc_esco_remove(struct ull_bredr_conn *conn, uint8_t esco_handle,
			 uint8_t reason);
void lmp_proc_esco_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);

/* Detach */
int lmp_proc_detach(struct ull_bredr_conn *conn, uint8_t reason);
void lmp_proc_detach_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);

/* Supervision Timeout */
int lmp_proc_supervision_timeout(struct ull_bredr_conn *conn, uint16_t timeout);
void lmp_proc_supervision_timeout_rx(struct ull_bredr_conn *conn,
				     uint8_t *pdu, uint8_t len);

/* Max Slots */
int lmp_proc_max_slots(struct ull_bredr_conn *conn, uint8_t max_slots);
void lmp_proc_max_slots_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);

/* Timing Accuracy */
int lmp_proc_timing_accuracy_request(struct ull_bredr_conn *conn);
void lmp_proc_timing_accuracy_rx(struct ull_bredr_conn *conn,
				 uint8_t *pdu, uint8_t len);

/* Power Control */
int lmp_proc_power_control(struct ull_bredr_conn *conn, int8_t delta);
void lmp_proc_power_control_rx(struct ull_bredr_conn *conn,
			       uint8_t *pdu, uint8_t len);

/* Packet Type Table */
int lmp_proc_packet_type_table(struct ull_bredr_conn *conn, uint8_t table);
void lmp_proc_packet_type_table_rx(struct ull_bredr_conn *conn,
				   uint8_t *pdu, uint8_t len);

/* Sniff Subrating */
int lmp_proc_sniff_subrating(struct ull_bredr_conn *conn,
			     uint16_t max_latency, uint16_t min_remote_timeout,
			     uint16_t min_local_timeout);
void lmp_proc_sniff_subrating_rx(struct ull_bredr_conn *conn,
				 uint8_t *pdu, uint8_t len);

/* Channel Classification */
int lmp_proc_channel_classification(struct ull_bredr_conn *conn, uint8_t *map);
void lmp_proc_channel_classification_rx(struct ull_bredr_conn *conn,
					uint8_t *pdu, uint8_t len);

/* Ping */
int lmp_proc_ping(struct ull_bredr_conn *conn);
void lmp_proc_ping_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);

/*
 * LMP State Machine Run
 * Called during connection prepare to process pending procedures
 * Similar to BLE's ull_cp_run()
 */
void lmp_run(struct ull_bredr_conn *conn);

/*
 * Connection Sequence Functions
 * Bluetooth Core Spec Vol 2, Part C - Connection establishment
 */
void lmp_proc_auth_cmp(struct ull_bredr_conn *conn, uint8_t reason);
void lmp_proc_calc_link_key(struct ull_bredr_conn *conn);
void lmp_proc_chg_pkt_type_cont(struct ull_bredr_conn *conn, uint8_t status);
void lmp_proc_chg_pkt_type_cmp(struct ull_bredr_conn *conn, uint8_t status);
void lmp_proc_chg_pkt_type_retry(struct ull_bredr_conn *conn);
void lmp_proc_comb_key_svr(struct ull_bredr_conn *conn, uint8_t *key);
void lmp_proc_con_cmp(struct ull_bredr_conn *conn);
void lmp_proc_con_cmp_evt_send(struct ull_bredr_conn *conn, uint8_t status);
void lmp_proc_enc_cmp(struct ull_bredr_conn *conn, uint8_t reason);
void lmp_proc_enc_key_refresh(struct ull_bredr_conn *conn);
void lmp_proc_end_chk_colli(struct ull_bredr_conn *conn, uint8_t enc_mode);

/*
 * EPR (Encryption Pause Resume) Functions
 */
void lmp_proc_epr_cmp(struct ull_bredr_conn *conn);
void lmp_proc_epr_change_lk(struct ull_bredr_conn *conn, uint8_t reason);
void lmp_proc_epr_resp(struct ull_bredr_conn *conn);
void lmp_proc_initiator_epr(struct ull_bredr_conn *conn);

/*
 * Key Exchange Functions
 */
void lmp_proc_key_exch_end(struct ull_bredr_conn *conn, uint8_t reason);
void lmp_proc_start_key_exch(struct ull_bredr_conn *conn);

/*
 * Pairing Functions
 */
void lmp_proc_legacy_pair(struct ull_bredr_conn *conn);
void lmp_proc_pairing_cont(struct ull_bredr_conn *conn);

/*
 * Role Switch Functions
 */
void lmp_proc_local_switch(struct ull_bredr_conn *conn);
void lmp_proc_rem_switch(struct ull_bredr_conn *conn);
void lmp_proc_rsw_done(struct ull_bredr_conn *conn, uint8_t status);
void lmp_proc_rsw_non_epr_back(struct ull_bredr_conn *conn);
void lmp_proc_switch_cmp(struct ull_bredr_conn *conn, uint8_t status);

/*
 * Max Slot Management
 */
void lmp_proc_max_slot_mgt(struct ull_bredr_conn *conn, uint8_t max_slot);

/*
 * Mutual Authentication Functions
 */
void lmp_proc_mutual_auth_end(struct ull_bredr_conn *conn, uint8_t reason);
void lmp_proc_mutual_auth_end2(struct ull_bredr_conn *conn, uint8_t reason);
void lmp_proc_init_start_mutual_auth(struct ull_bredr_conn *conn);

/*
 * Master Key Functions
 */
void lmp_proc_mst_key(struct ull_bredr_conn *conn);
void lmp_proc_mst_send_mst_key(struct ull_bredr_conn *conn);
void lmp_proc_mst_qos_done(struct ull_bredr_conn *conn);

/*
 * Passkey Functions
 */
void lmp_proc_passkey_comm(struct ull_bredr_conn *conn);
void lmp_proc_start_passkey(struct ull_bredr_conn *conn);
void lmp_proc_start_passkey_loop(struct ull_bredr_conn *conn);
void lmp_proc_init_passkey_loop(struct ull_bredr_conn *conn);

/*
 * OOB Functions
 */
void lmp_proc_start_oob(struct ull_bredr_conn *conn);
void lmp_proc_skip_hl_oob_req(struct ull_bredr_conn *conn);
void lmp_proc_resp_oob_wait_nonce(struct ull_bredr_conn *conn);
void lmp_proc_resp_oob_nonce(struct ull_bredr_conn *conn);

/*
 * Packet Type Table Functions
 */
void lmp_proc_ptt(struct ull_bredr_conn *conn);
void lmp_proc_ptt_cmp(struct ull_bredr_conn *conn);

/*
 * Remote Name Functions
 */
void lmp_proc_rd_rem_name(struct ull_bredr_conn *conn);
void lmp_proc_rem_name_cont(struct ull_bredr_conn *conn);

/*
 * Remote Encryption Functions
 */
void lmp_proc_rem_enc(struct ull_bredr_conn *conn);

/*
 * Responder Functions
 */
void lmp_proc_resp_sec_auth(struct ull_bredr_conn *conn);
void lmp_proc_resp_auth(struct ull_bredr_conn *conn);
void lmp_proc_resp_calc_f3(struct ull_bredr_conn *conn);
void lmp_proc_resp_num_comp(struct ull_bredr_conn *conn);
void lmp_proc_resp_pair(struct ull_bredr_conn *conn);

/*
 * Release/Detach Functions
 */
void lmp_proc_release(struct ull_bredr_conn *conn);

/*
 * Restart Encryption Functions
 */
void lmp_proc_restart_enc(struct ull_bredr_conn *conn);
void lmp_proc_restart_enc_cont(struct ull_bredr_conn *conn, uint8_t reason);

/*
 * Timeout Functions
 */
void lmp_proc_restore_to(struct ull_bredr_conn *conn);
void lmp_proc_start_lmp_to(struct ull_bredr_conn *conn);
void lmp_proc_stop_lmp_to(struct ull_bredr_conn *conn);

/*
 * Encryption Start/Stop Functions
 */
void lmp_proc_start_enc(struct ull_bredr_conn *conn);
void lmp_proc_start_enc_key_size(struct ull_bredr_conn *conn);
void lmp_proc_stop_enc(struct ull_bredr_conn *conn);
void lmp_proc_send_enc_mode(struct ull_bredr_conn *conn);
void lmp_proc_send_enc_chg_evt(uint8_t idx, uint8_t status);

/*
 * Semi-Permanent Key Functions
 */
void lmp_proc_semi_key_cmp(struct ull_bredr_conn *conn, uint8_t reason);

/*
 * Simple Pairing Functions
 */
void lmp_proc_sp_fail(struct ull_bredr_conn *conn);
void lmp_proc_sp_end(uint8_t link_id, uint8_t reason);
void lmp_proc_dhkey(struct ull_bredr_conn *conn);
void lmp_proc_init_calc_f3(struct ull_bredr_conn *conn);

/*
 * Packet Type Functions
 */
void lmp_proc_packet_type(struct ull_bredr_conn *conn, int idx);

/*
 * Local Authentication Functions
 */
void lmp_proc_loc_auth(struct ull_bredr_conn *conn);

/*
 * Extended Features Functions
 */
void lmp_proc_ext_feat(struct ull_bredr_conn *conn, uint8_t page_nb);
void lmp_proc_feat(struct ull_bredr_conn *conn);

/*
 * Host Connection Functions
 */
void lmp_proc_hl_connect(struct ull_bredr_conn *conn);

/*
 * Version Functions
 */
void lmp_proc_version(struct ull_bredr_conn *conn);

/*
 * Secure Authentication Functions
 */
void lmp_proc_sec_auth_compute_sres(struct ull_bredr_conn *conn);

/*
 * Helper Functions
 */
bool lmp_proc_get_feature(uint8_t *features, uint8_t bit_pos);
uint8_t lmp_proc_max_slot(uint16_t packet_type);
void lmp_proc_suppress_acl_packet(uint16_t *packet_type, uint8_t max_slot);

#endif /* SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_LMP_PROC_H_ */
