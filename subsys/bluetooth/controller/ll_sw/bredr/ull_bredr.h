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
 * BR/EDR Upper Link Layer (ULL) - Link Manager Interface
 * Corresponds to Bluetooth Core Spec Vol 2, Part C: Link Manager Protocol
 */

#ifndef SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_ULL_BREDR_H_
#define SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_ULL_BREDR_H_

#include <zephyr/types.h>
#include <zephyr/sys/slist.h>
#include "../ull_internal.h"
#include "lll_bredr.h"
#include "pdu_bredr.h"

/*
 * BR/EDR ULL Configuration
 */
#if defined(CONFIG_BT_CTLR_BREDR_MAX_CONN)
#define ULL_BREDR_MAX_CONN CONFIG_BT_CTLR_BREDR_MAX_CONN
#else
#define ULL_BREDR_MAX_CONN 4
#endif

#if defined(CONFIG_BT_CTLR_BREDR_SCO_MAX)
#define ULL_BREDR_SCO_MAX CONFIG_BT_CTLR_BREDR_SCO_MAX
#else
#define ULL_BREDR_SCO_MAX 2
#endif

#if defined(CONFIG_BT_CTLR_BREDR_ESCO_MAX)
#define ULL_BREDR_ESCO_MAX CONFIG_BT_CTLR_BREDR_ESCO_MAX
#else
#define ULL_BREDR_ESCO_MAX 2
#endif

/*
 * LT Address Management
 */
#define LT_ADDR_MIN             1
#define LT_ADDR_MAX             7

/*
 * Default Parameters
 */
#define LSTO_DFT                0x7D00  /* 20 seconds */
#define POLL_INTERVAL_DFT       0x0028  /* 40 slots = 25ms */
#define CON_ACCEPT_TO_DFT       0x1F40  /* 5 seconds */
#define PAGE_TO_DFT             0x2000  /* 5.12 seconds */
#define INQ_SCAN_INTV_DFT       0x1000  /* 2.56 seconds */
#define INQ_SCAN_WIN_DFT        0x0012  /* 11.25ms */
#define PAGE_SCAN_INTV_DFT      0x0800  /* 1.28 seconds */
#define PAGE_SCAN_WIN_DFT       0x0012  /* 11.25ms */
#define AUTH_PAYL_TO_DFT        0x0BB8  /* 30 seconds */

/*
 * AFH Constants
 */
#define AFH_DISABLED            0
#define AFH_ENABLED             1
#define AFH_REPORTING_ENABLED   1
#define AFH_REPORTING_DISABLED  0
#define BT_AFH_UPDATE_PERIOD    30      /* seconds */
#define BT_AFH_CH_CLASS_INT_MIN 0x0640  /* 1 second */
#define BT_AFH_CH_CLASS_INT_MAX 0x7D00  /* 20 seconds */

/*
 * SAM (Slot Availability Mask) Constants
 */
#define SAM_DISABLED            0xFF
#define SAM_INDEX_MAX           4
#define SAM_SLOTS_UNAVAILABLE   0x00
#define SAM_SLOTS_AVAILABLE     0x01
#define SAM_SLOTS_SUBMAPPED     0x02
#define SAM_SLOT_TX_AVAILABLE   0x01
#define SAM_SLOT_RX_AVAILABLE   0x02
#define SAM_SLOT_TX_RX_AVAILABLE 0x03
#define SAM_TYPE0_SUBMAP_LEN    14
#define SAM_SUBMAPS_LEN         12

/*
 * Power Modes
 */
#define LM_ACTIVE_MODE          0
#define LM_HOLD_MODE            1
#define LM_SNIFF_MODE           2
#define LM_PARK_MODE            3

/*
 * Packet Type Tables
 */
#define PACKET_TABLE_1MBPS      0
#define PACKET_TABLE_2_3MBPS    1

/*
 * QoS Service Types
 */
#define QOS_NO_TRAFFIC          0
#define QOS_BEST_EFFORT         1
#define QOS_GUARANTEED          2

/*
 * Link Controller State Machine
 * Bluetooth Core Spec Vol 2, Part C - Connection state management
 */
enum bredr_lc_state {
	BREDR_LC_FREE = 0,
	BREDR_LC_CONNECTED,
	/* Version/Features exchange */
	BREDR_LC_WAIT_VERSION_CENTRAL,
	BREDR_LC_WAIT_ENC_SIZE_MASK,
	BREDR_LC_WAIT_HOST_CONN,
	BREDR_LC_WAIT_NAME,
	BREDR_LC_WAIT_VERSION,
	BREDR_LC_WAIT_FEATURES,
	BREDR_LC_WAIT_EXT_FEATURES,
	/* Disconnection */
	BREDR_LC_WAIT_DETACH_TX_CFM,
	BREDR_LC_WAIT_DISC_TIMEOUT,
	BREDR_LC_WAIT_DISC_CFM,
	BREDR_LC_WAIT_DETACH_TIMEOUT,
	/* Pairing/Authentication */
	BREDR_LC_WAIT_PAIR_CFM_INIT,
	BREDR_LC_WAIT_PAIR_CFM_RSP,
	BREDR_LC_WAIT_AUTH_SRES,
	BREDR_LC_WAIT_KEY_EXCH,
	BREDR_LC_WAIT_KEY_EXCH_RSP,
	BREDR_LC_WAIT_AU_RAND_RSP,
	BREDR_LC_WAIT_AU_RAND_PEER,
	BREDR_LC_WAIT_LINK_KEY,
	BREDR_LC_WAIT_PIN_CODE,
	BREDR_LC_WAIT_SRES_RSP,
	BREDR_LC_WAIT_SRES_INIT,
	/* Encryption */
	BREDR_LC_WAIT_ENC_MODE_CFM,
	BREDR_LC_WAIT_ENC_SIZE_CFM,
	BREDR_LC_WAIT_ENC_STOP_CFM,
	BREDR_LC_WAIT_ENC_START_CFM,
	BREDR_LC_WAIT_ENC_PERIPH_SIZE,
	BREDR_LC_WAIT_ENC_PERIPH_RESTART,
	/* EPR (Encryption Pause Resume) */
	BREDR_LC_WAIT_EPR_STOP_PERIPH,
	BREDR_LC_WAIT_ENC_START_PERIPH,
	BREDR_LC_WAIT_EPR_PAUSE_CENTRAL,
	BREDR_LC_WAIT_EPR_RSP,
	BREDR_LC_WAIT_EPR_RESUME,
	BREDR_LC_WAIT_EPR_RESUME_CENTRAL,
	BREDR_LC_WAIT_EPR_PEER_CENTRAL,
	BREDR_LC_WAIT_EPR_PEER_PERIPH,
	BREDR_LC_WAIT_EPR_PAUSE_RSP,
	BREDR_LC_WAIT_EPR_STOP_RSP,
	BREDR_LC_WAIT_RESTART_ENC,
	/* Sniff mode */
	BREDR_LC_WAIT_SNIFF_REQ,
	BREDR_LC_WAIT_SNIFF_ACC_CFM,
	BREDR_LC_WAIT_SNIFF_SUB_RSP,
	BREDR_LC_WAIT_SNIFF_SUB_CFM,
	BREDR_LC_WAIT_UNSNIFF_ACC,
	BREDR_LC_WAIT_UNSNIFF_CFM,
	BREDR_LC_WAIT_SSR_INSTANT,
	/* Role switch */
	BREDR_LC_WAIT_SWITCH_CFM,
	BREDR_LC_WAIT_SWITCH_CMP,
	BREDR_LC_WAIT_SWITCH_SEMI_ACC,
	/* Packet type table */
	BREDR_LC_WAIT_PKT_TYPE_ACC_CFM,
	BREDR_LC_WAIT_PTT_ACC_CFM,
	/* SSP (Secure Simple Pairing) */
	BREDR_LC_WAIT_IO_CAP_INIT,
	BREDR_LC_WAIT_IO_CAP_RSP,
	BREDR_LC_WAIT_IO_CAP_CFM,
	BREDR_LC_PUB_KEY_HDR_INIT,
	BREDR_LC_PUB_KEY_DATA_INIT,
	BREDR_LC_PUB_KEY_HDR_RSP_CFM,
	BREDR_LC_PUB_KEY_DATA_RSP_CFM,
	BREDR_LC_WAIT_PUB_KEY_HDR_PEER,
	BREDR_LC_WAIT_PUB_KEY_DATA_PEER,
	BREDR_LC_WAIT_PUB_KEY_HDR_RSP,
	BREDR_LC_WAIT_PUB_KEY_DATA_RSP,
	BREDR_LC_WAIT_NC_COMMIT_CFM,
	BREDR_LC_WAIT_NC_NONCE_PEER,
	BREDR_LC_WAIT_NC_NONCE_CFM,
	BREDR_LC_WAIT_NC_USER_CFM_INIT,
	BREDR_LC_WAIT_NC_USER_CFM_RSP,
	BREDR_LC_WAIT_NC_NONCE_RSP_PEER,
	BREDR_LC_WAIT_NC_NONCE_RSP_CFM,
	BREDR_LC_WAIT_DHKEY_RSP,
	BREDR_LC_WAIT_DHKEY_CHECK_PEER,
	BREDR_LC_WAIT_DHKEY_CHECK_CFM,
	BREDR_LC_WAIT_DHKEY_CHECK_RSP_CFM,
	BREDR_LC_WAIT_PASSKEY_REPLY,
	BREDR_LC_WAIT_PASSKEY_COMMIT_RSP,
	BREDR_LC_WAIT_PASSKEY_COMMIT_INIT,
	BREDR_LC_WAIT_PASSKEY_NONCE_CFM,
	BREDR_LC_WAIT_PASSKEY_NONCE_PEER,
	BREDR_LC_WAIT_PASSKEY_NONCE_RSP,
	BREDR_LC_WAIT_PASSKEY_NONCE_RSP_CFM,
	BREDR_LC_WAIT_OOB_DATA,
	BREDR_LC_WAIT_OOB_NONCE_RSP,
	BREDR_LC_WAIT_OOB_NONCE_RSP_CFM,
	BREDR_LC_WAIT_OOB_NONCE_INIT,
	BREDR_LC_WAIT_OOB_NONCE_INIT_CFM,
	BREDR_LC_WAIT_DHKEY_COMPUTE,
	BREDR_LC_WAIT_AUTH_NC_NONCE,
	BREDR_LC_WAIT_AUTH_PK_NONCE,
	BREDR_LC_WAIT_DHKEY_FAIL,
	BREDR_LC_WAIT_SEC_AUTH_RAND,
	BREDR_LC_WAIT_SEC_AUTH_SRES,
	/* Misc */
	BREDR_LC_WAIT_CLK_OFFSET,
	BREDR_LC_WAIT_TIMING_ACC,
	BREDR_LC_WAIT_QOS_CFM,
	BREDR_LC_WAIT_ACL_ACC,
	BREDR_LC_WAIT_TEST_TX_CFM,
	BREDR_LC_WAIT_MAX_SLOT_CFM,
	BREDR_LC_NAME_PAGE_CANCEL,
	BREDR_LC_WAIT_PING_RSP,
	BREDR_LC_WAIT_PWR_CTRL_RSP,
	BREDR_LC_WAIT_PWR_CTRL_CFM,
	/* SCO */
	BREDR_LC_SCO_NEGO_ONGOING,
	BREDR_LC_SCO_DISC_ONGOING,
	/* PCA (Piconet Clock Adjust) */
	BREDR_LC_WAIT_CLK_ADJ_ACC_CFM,
	BREDR_LC_WAIT_CLK_ADJ_CFM,
	/* SAM (Slot Availability Mask) */
	BREDR_LC_WAIT_SAM_TYPE0_CFM,
	BREDR_LC_WAIT_SAM_MAP_CFM,
	BREDR_LC_WAIT_SAM_SWITCH_CFM,
	/* Debug */
	BREDR_LC_WAIT_DBG_LMP_CFM,
	/* Max state */
	BREDR_LC_STATE_MAX
};

/*
 * Link Manager State
 * Bluetooth Core Spec Vol 2, Part C - Link state management
 */
enum bredr_link_state {
	BREDR_LINK_FREE = 0,
	BREDR_LINK_PAGE,
	BREDR_LINK_PAGE_STOPPING,
	BREDR_LINK_PAGE_SCAN,
	BREDR_LINK_PAGE_SCAN_STOPPING,
	BREDR_LINK_CONNECTED,
	BREDR_LINK_SWITCH,
};

/*
 * BR/EDR ULL Inquiry Context
 */
struct ull_bredr_inquiry {
	struct ull_hdr ull;
	struct lll_bredr_inquiry lll;

	/* Ticker ID */
	uint8_t ticker_id;

	/* Callback */
	void (*complete_cb)(uint8_t status);
};

/*
 * BR/EDR ULL Inquiry Scan Context
 */
struct ull_bredr_inquiry_scan {
	struct ull_hdr ull;
	struct lll_bredr_inquiry_scan lll;

	/* Ticker ID */
	uint8_t ticker_id;

	/* EIR data */
	uint8_t eir_data[240];
	uint8_t eir_len;
};

/*
 * BR/EDR ULL Page Context
 */
struct ull_bredr_page {
	struct ull_hdr ull;
	struct lll_bredr_page lll;

	/* Ticker ID */
	uint8_t ticker_id;

	/* Callback */
	void (*complete_cb)(uint8_t status, uint16_t handle);
};

/*
 * BR/EDR ULL Page Scan Context
 */
struct ull_bredr_page_scan {
	struct ull_hdr ull;
	struct lll_bredr_page_scan lll;

	/* Ticker ID */
	uint8_t ticker_id;
};

/*
 * BR/EDR Link Structure
 * Bluetooth Core Spec Vol 2, Part C - Link parameters
 */
struct ull_bredr_link {
	/* QoS parameters */
	uint32_t token_rate;
	uint32_t peak_bandwidth;
	uint32_t latency;
	uint32_t delay_variation;
	uint32_t access_latency;
	uint32_t token_bucket_size;
	uint32_t switch_instant;

	/* Link parameters */
	uint16_t link_timeout;
	uint16_t acl_packet_type;
	uint16_t cur_packet_type;
	uint16_t link_policy_settings;
	uint16_t poll_interval;
	uint16_t slot_offset;
	uint16_t failed_contact;
	uint16_t rx_preferred_rate;
	uint16_t auth_payl_to;
	uint16_t auth_payl_to_margin;

	/* Device class */
	uint8_t  class_of_device[3];

	/* Slot management */
	uint8_t  tx_max_slot_cur;
	uint8_t  max_slot_received;

	/* Role and state */
	uint8_t  allow_role_switch;
	uint8_t  role;
	uint8_t  reason;
	uint8_t  lt_addr;

	/* Transaction IDs */
	uint8_t  rx_tr_id_server;
	uint8_t  rx_tr_id_client;
	uint8_t  rx_pkt_type_id;

	/* Mode and flow */
	uint8_t  current_mode;
	uint8_t  flow_direction;
	uint8_t  service_type;
	uint8_t  cur_packet_type_table;
	uint8_t  ptt_tmp;
	uint8_t  flags;
	uint8_t  micerr_cnt;

	/* State flags */
	bool     initiator;
	bool     connected_state;
	bool     setup_complete;
	bool     setup_comp_rx;
	bool     setup_comp_tx;
	bool     connection_complete_sent;
	bool     host_connected;
	bool     flush_continue;
	bool     min_power_rcv;
	bool     max_power_rcv;
	bool     packet_type_table_2mb;
	bool     qos_notified;
	bool     epc_supported;
	bool     l2cap_start;
	bool     esco_loopback_mode;
};

/*
 * BR/EDR Info Structure
 * Bluetooth Core Spec Vol 2, Part C - Remote device information
 */
struct ull_bredr_info {
	/* Remote features (including extended) */
	uint8_t  remote_features[3][8];  /* 3 pages */
	uint8_t  remote_feat_rec;        /* Bit field for received pages */

	/* Addresses */
	uint8_t  local_bd_addr[6];
	uint8_t  bd_addr[6];

	/* Remote version */
	uint8_t  remote_vers;
	uint16_t remote_comp_id;
	uint16_t remote_subvers;
	bool     recv_rem_ver_rec;

	/* Remote name */
	uint8_t  remote_name[248];
	uint8_t  remote_name_len;
	bool     send_remote_name_cfm;
};

/*
 * BR/EDR Encryption Structure
 * Bluetooth Core Spec Vol 2, Part H - Encryption parameters
 */
struct ull_bredr_enc {
	/* Key management */
	uint8_t  key_from_host;
	uint16_t key_size_mask;
	uint8_t  key_type;
	uint8_t  key_flag;
	uint8_t  new_key_flag;
	uint8_t  key_status;
	uint8_t  pin_status;
	uint8_t  pin_length;
	uint8_t  enc_size;
	uint8_t  enc_mode;
	uint8_t  new_enc_mode;
	uint8_t  enc_enable;

	/* Keys */
	uint8_t  auth_key[16];
	uint8_t  lt_key[16];
	uint8_t  semi_permanent_key[16];
	uint8_t  overlay[16];
	uint8_t  random_rx[16];
	uint8_t  random_tx[16];
	uint8_t  enc_key[16];
	uint8_t  encryption_key[16];  /* Current encryption key */

	/* Authentication */
	uint8_t  sres[4];
	uint8_t  sres_expected[4];
	uint8_t  aco[12];
	uint8_t  pin_code[16];

	/* Flags */
	bool     link_key_valid;
	bool     prevent_enc_evt;
};

/*
 * BR/EDR Request Structure
 * Tracks pending LMP procedure requests
 */
struct ull_bredr_req {
	/* Local requests */
	bool loc_name_req;
	bool loc_remote_extended_req;
	bool loc_detach_req;
	bool loc_cpt_req;
	bool loc_enc_req;
	bool loc_auth_req;
	bool loc_key_exchange_req;
	bool loc_enc_key_refresh;
	bool loc_switch_req;
	bool loc_vers_req;
	bool loc_flow_spec_req;

	/* Peer requests */
	bool peer_switch_req;
	bool peer_auth_req;
	bool peer_enc_req;
	bool peer_detach_req;
	bool peer_enc_key_refresh;

	/* Internal requests */
	bool restart_enc_req;
	bool master_key_req;
};

/*
 * BR/EDR AFH Structure
 * Bluetooth Core Spec Vol 2, Part B - Adaptive Frequency Hopping
 */
struct ull_bredr_afh {
	/* Channel maps */
	uint8_t  ch_map[10];
	uint8_t  ch_class[10];

	/* Reporting */
	uint32_t reporting_interval;

	/* Flags */
	bool     en;
	bool     temp_en;
	bool     reporting_en;
	bool     lmp_ch_class_pending;
};

/*
 * BR/EDR Simple Pairing Structure
 * Bluetooth Core Spec Vol 2, Part H - Secure Simple Pairing
 */
struct ull_bredr_sp {
	/* Passkey */
	uint32_t passkey;
	uint8_t  passkey_bit;  /* Current passkey bit position (0-19) */

	/* Random numbers */
	uint8_t  loc_rand_n[16];
	uint8_t  rem_rand_n[16];
	uint8_t  nonce_loc[16];
	uint8_t  nonce_rem[16];

	/* Commitment values */
	uint8_t  loc_commitment[16];
	uint8_t  rem_commitment[16];

	/* DH Key */
	uint8_t  dhkey[32];

	/* DH Key check */
	uint8_t  dhkey_check[16];

	/* IO capabilities */
	uint8_t  io_cap_loc[3];  /* io_cap, oob_data_present, auth_req */
	uint8_t  io_cap_rem[3];

	/* Encapsulated PDU counter */
	uint8_t  encap_pdu_ctr;

	/* State */
	uint8_t  sp_phase1_failed;
	uint8_t  sp_dhkey;
	uint8_t  sp_tid;
	bool     sp_initiator;
	bool     sec_con;
	bool     mitm_protection;
};

/*
 * BR/EDR EPR Structure
 * Bluetooth Core Spec Vol 2, Part C - Encryption Pause Resume
 */
struct ull_bredr_epr {
	bool     on;
	bool     rsw;
	uint8_t  rsw_error;
	bool     cclk;
	bool     flow_off;
	bool     pause_pending;
	bool     resume_pending;
	uint8_t  random[16];
};

/*
 * BR/EDR Local Transaction Details
 * Tracks local LMP transaction state
 */
struct ull_bredr_local_trans {
	uint8_t  opcode;
	uint8_t  opcode_ext;
	uint8_t  in_use;
};

/*
 * BR/EDR SAM Pattern
 * Bluetooth Core Spec Vol 2, Part B - Slot Availability Mask pattern
 */
struct ull_bredr_sam_pattern {
	uint16_t n_tx_slots;
	uint16_t n_rx_slots;
	uint8_t  submaps[SAM_SUBMAPS_LEN];
	uint8_t  t_sam_sm;
	uint8_t  n_sam_sm;
	uint8_t  n_ex_sm;
};

/*
 * BR/EDR SAM Structure
 * Bluetooth Core Spec Vol 2, Part B - Slot Availability Mask
 */
struct ull_bredr_sam {
	uint32_t instant;
	uint16_t t_sam;
	uint16_t n_tx_slots;
	uint16_t n_rx_slots;
	uint16_t loc_t_sam_av;
	uint16_t rem_t_sam_av;
	uint8_t  rem_submap[SAM_TYPE0_SUBMAP_LEN];
	struct ull_bredr_sam_pattern rem_pattern[SAM_INDEX_MAX];
	uint8_t  rem_idx;
	uint8_t  loc_idx;
	uint8_t  loc_idx_wait_cfm;
	uint8_t  t_sam_sm;
	uint8_t  loc_tx_av;
	uint8_t  loc_rx_av;
	uint8_t  rem_tx_av;
	uint8_t  rem_rx_av;
	bool     loc_submap0_av;
	bool     loc_pattern_av[SAM_INDEX_MAX];
	bool     rem_submap0_av;
	bool     rem_pattern_av[SAM_INDEX_MAX];
	bool     config_mode;
};

/*
 * BR/EDR ULL ACL Connection Context
 * Bluetooth Core Spec Vol 2, Part C - ACL connection management
 */
struct ull_bredr_conn {
	struct ull_hdr ull;
	struct lll_bredr_conn lll;

	/* Ticker ID */
	uint8_t ticker_id;

	/* Link Controller state */
	uint8_t  lc_state;

	/* Sub-structures */
	struct ull_bredr_link link;
	struct ull_bredr_info info;
	struct ull_bredr_enc  enc;
	struct ull_bredr_req  req;
	struct ull_bredr_afh  afh;
	struct ull_bredr_sp   sp;
	struct ull_bredr_epr  epr;
	struct ull_bredr_local_trans local_trans;
	struct ull_bredr_sam  sam_info;

	/* Pending LMP transactions */
	sys_slist_t lmp_tx_pending;
	sys_slist_t lmp_rx_pending;

	/* Sniff parameters */
	uint16_t sniff_interval;
	uint16_t sniff_attempt;
	uint16_t sniff_timeout;
	uint16_t sniff_offset;

	/* TX queue */
	struct {
		void *head;
		void *tail;
	} tx_queue;

	/* Supervision */
	uint32_t supervision_expire;
	uint16_t supervision_timeout;

	/* Remote version info */
	uint8_t  remote_version;
	uint16_t remote_company_id;
	uint16_t remote_subversion;

	/* Remote features */
	uint8_t  remote_features[8];

	/* Remote name */
	uint8_t  remote_name[248];
	uint8_t  remote_name_len;

	/* Link key */
	uint8_t  link_key[16];
	uint8_t  link_key_type;

	/* Authentication state */
	uint8_t  auth_state;
	uint8_t  auth_pending;
	uint8_t  enc_pending;
	uint8_t  auth_au_rand[16];
	uint8_t  auth_aco[12];

	/* SSP state */
	uint8_t  ssp_state;
	uint8_t  io_capability;
	uint8_t  oob_data_present;
	uint8_t  auth_requirements;

	/* Role switch */
	uint8_t  role_switch_pending;

	/* Power mode */
	uint8_t  power_mode;

	/* Pairing data */
	uint8_t  pairing_in_rand[16];
	uint8_t  pairing_init_key[16];
	uint8_t  pairing_comb_key[16];

	/* LMP timeout restart flag */
	bool     lmp_timeout_restart;

	/* Callbacks */
	void (*disconnect_cb)(uint8_t reason);
};

/*
 * BR/EDR ULL SCO Connection Context
 */
struct ull_bredr_sco {
	struct ull_hdr ull;
	struct lll_bredr_sco lll;

	/* Ticker ID */
	uint8_t ticker_id;

	/* Parent ACL connection handle */
	uint16_t acl_handle;

	/* Voice settings */
	uint16_t voice_setting;
};

/*
 * BR/EDR ULL eSCO Connection Context
 */
struct ull_bredr_esco {
	struct ull_hdr ull;
	struct lll_bredr_esco lll;

	/* Ticker ID */
	uint8_t ticker_id;

	/* Parent ACL connection handle */
	uint16_t acl_handle;

	/* Voice settings */
	uint16_t voice_setting;
};

/*
 * BR/EDR ULL LMP Transaction
 */
struct ull_bredr_lmp_tx {
	sys_snode_t node;
	uint8_t opcode;
	uint8_t tid;
	uint8_t data[17];             /* Max LMP PDU payload */
	uint8_t len;
	uint8_t retries;
};

/*
 * BR/EDR HCI Configuration Parameters
 * Bluetooth Core Spec Vol 4, Part E - HCI configuration
 */
struct ull_bredr_hci_cfg {
	uint8_t  scan_en;
	uint16_t inq_scan_intv;
	uint16_t inq_scan_win;
	uint8_t  inq_scan_type;
	uint8_t  inq_mode;
	uint16_t page_to;
	uint16_t con_accept_to;
	uint16_t page_scan_intv;
	uint16_t page_scan_win;
	uint8_t  page_scan_rep_mode;
	uint8_t  page_scan_type;
	uint16_t voice_stg;
	uint8_t  pin_type;
	uint8_t  auth_en;
	uint16_t ext_page_to;
	uint16_t ext_inq_len;
	int8_t   inq_tx_pwr_lvl;
	uint8_t  iac_lap[3];
	uint16_t link_pol_stg;
	uint8_t  sp_mode;
	uint8_t  sec_con_host_supp;
	uint8_t  le_supported_host;
	uint8_t  simultaneous_le_host;
	uint8_t  loopback_mode;
	uint8_t  sp_debug_mode;
	uint8_t  err_data_rep;
	bool     dut_mode_en;
	bool     ch_ass_en;
	bool     sync_flow_ctrl_en;
};

/*
 * BR/EDR AFH Global Parameters
 * Bluetooth Core Spec Vol 2, Part B - Global AFH state
 */
struct ull_bredr_afh_global {
	uint8_t  host_ch_class[10];
	uint8_t  peer_ch_class[ULL_BREDR_MAX_CONN][10];
	uint8_t  master_ch_map[10];
	uint8_t  active;
};

/*
 * BR/EDR SAM Global Info
 * Bluetooth Core Spec Vol 2, Part B - Global SAM state
 */
struct ull_bredr_sam_global {
	uint8_t  type0_submap[SAM_TYPE0_SUBMAP_LEN];
	struct ull_bredr_sam_pattern pattern[SAM_INDEX_MAX];
	uint8_t  frame_len;
	int16_t  frame_offset;
	uint8_t  active_index;
};

/*
 * BR/EDR Connection Info
 * Per-connection state tracking
 */
struct ull_bredr_con_info {
	uint8_t  bd_addr[6];
	uint8_t  state;
	uint8_t  role;
	uint8_t  lt_addr;
};

/*
 * BR/EDR LM Environment
 * Global Link Manager state
 */
struct ull_bredr_lm_env {
	/* HCI configuration */
	struct ull_bredr_hci_cfg hci;

	/* Connection info */
	struct ull_bredr_con_info con_info[ULL_BREDR_MAX_CONN];

	/* LT Address bitmap */
	uint8_t  lt_addr_bitmap;

	/* Local name */
	char    *local_name;

	/* Inquiry state */
	uint8_t  inq_state;

	/* Keys */
	uint8_t  priv_key_192[24];
	uint8_t  pub_key_192[48];
	uint8_t  priv_key_256[32];
	uint8_t  pub_key_256[64];

	/* OOB data */
	uint8_t  oob_r[16];
	uint8_t  oob_c[16];

	/* AFH parameters */
	struct ull_bredr_afh_global afh;

	/* SAM parameters */
	struct ull_bredr_sam_global sam_info;

	/* Channel assessment */
	bool     ch_ass_en;

	/* SCO move enable */
	bool     sco_move_en;
};

/*
 * BR/EDR ULL Function Declarations
 */

/* Initialization */
int ull_bredr_init(void);
int ull_bredr_reset(void);

/* Inquiry */
int ull_bredr_inquiry_start(uint8_t *lap, uint8_t length, uint8_t num_responses);
int ull_bredr_inquiry_stop(void);
int ull_bredr_inquiry_scan_enable(uint8_t enable);
int ull_bredr_inquiry_scan_set_params(uint16_t interval, uint16_t window);

/* Page */
int ull_bredr_page_start(uint8_t *bd_addr, uint8_t page_scan_rep_mode,
			 uint16_t clock_offset);
int ull_bredr_page_stop(void);
int ull_bredr_page_scan_enable(uint8_t enable);
int ull_bredr_page_scan_set_params(uint16_t interval, uint16_t window);

/* Connection management */
struct ull_bredr_conn *ull_bredr_conn_acquire(void);
void ull_bredr_conn_release(struct ull_bredr_conn *conn);
struct ull_bredr_conn *ull_bredr_conn_get(uint16_t handle);
struct ull_bredr_conn *ull_bredr_conn_get_by_addr(uint8_t *bd_addr);
uint16_t ull_bredr_conn_handle_get(struct ull_bredr_conn *conn);
int ull_bredr_conn_disconnect(uint16_t handle, uint8_t reason);

/* LMP procedures */
int ull_bredr_lmp_version_req(struct ull_bredr_conn *conn);
int ull_bredr_lmp_features_req(struct ull_bredr_conn *conn);
int ull_bredr_lmp_name_req(struct ull_bredr_conn *conn, uint8_t offset);
int ull_bredr_lmp_detach(struct ull_bredr_conn *conn, uint8_t reason);
int ull_bredr_lmp_host_conn_req(struct ull_bredr_conn *conn);
int ull_bredr_lmp_setup_complete(struct ull_bredr_conn *conn);

/* Authentication */
int ull_bredr_lmp_au_rand(struct ull_bredr_conn *conn, uint8_t *random);
int ull_bredr_lmp_sres(struct ull_bredr_conn *conn, uint8_t *sres);
int ull_bredr_lmp_in_rand(struct ull_bredr_conn *conn, uint8_t *random);
int ull_bredr_lmp_comb_key(struct ull_bredr_conn *conn, uint8_t *random);

/* Encryption */
int ull_bredr_lmp_encryption_mode_req(struct ull_bredr_conn *conn, uint8_t mode);
int ull_bredr_lmp_encryption_key_size_req(struct ull_bredr_conn *conn,
					  uint8_t key_size);
int ull_bredr_lmp_start_encryption_req(struct ull_bredr_conn *conn,
				       uint8_t *random);
int ull_bredr_lmp_stop_encryption_req(struct ull_bredr_conn *conn);

/* Secure Simple Pairing */
int ull_bredr_lmp_io_capability_req(struct ull_bredr_conn *conn,
				    uint8_t io_cap, uint8_t oob, uint8_t auth_req);
int ull_bredr_lmp_io_capability_res(struct ull_bredr_conn *conn,
				    uint8_t io_cap, uint8_t oob, uint8_t auth_req);
int ull_bredr_lmp_sp_confirm(struct ull_bredr_conn *conn, uint8_t *value);
int ull_bredr_lmp_sp_number(struct ull_bredr_conn *conn, uint8_t *nonce);
int ull_bredr_lmp_dhkey_check(struct ull_bredr_conn *conn, uint8_t *value);

/* Power modes */
int ull_bredr_lmp_hold_req(struct ull_bredr_conn *conn, uint16_t hold_time,
			   uint32_t hold_instant);
int ull_bredr_lmp_sniff_req(struct ull_bredr_conn *conn, uint16_t d_sniff,
			    uint16_t t_sniff, uint16_t attempt, uint16_t timeout);
int ull_bredr_lmp_unsniff_req(struct ull_bredr_conn *conn);
int ull_bredr_lmp_park_req(struct ull_bredr_conn *conn);
int ull_bredr_lmp_unpark_req(struct ull_bredr_conn *conn);

/* Role switch */
int ull_bredr_lmp_switch_req(struct ull_bredr_conn *conn, uint32_t instant);

/* AFH */
int ull_bredr_lmp_set_afh(struct ull_bredr_conn *conn, uint32_t instant,
			  uint8_t mode, uint8_t *channel_map);

/* QoS */
int ull_bredr_lmp_qos_req(struct ull_bredr_conn *conn, uint16_t poll_interval,
			  uint8_t nbc);

/* SCO/eSCO */
struct ull_bredr_sco *ull_bredr_sco_acquire(void);
void ull_bredr_sco_release(struct ull_bredr_sco *sco);
int ull_bredr_sco_start(struct ull_bredr_sco *sco, uint8_t t_sco);
int ull_bredr_sco_stop(struct ull_bredr_sco *sco);
int ull_bredr_lmp_sco_link_req(struct ull_bredr_conn *conn, uint8_t sco_handle,
			       uint8_t d_sco, uint8_t t_sco, uint8_t packet,
			       uint8_t air_mode);
int ull_bredr_lmp_remove_sco_link_req(struct ull_bredr_conn *conn,
				      uint8_t sco_handle, uint8_t reason);

struct ull_bredr_esco *ull_bredr_esco_acquire(void);
void ull_bredr_esco_release(struct ull_bredr_esco *esco);
int ull_bredr_esco_start(struct ull_bredr_esco *esco, uint8_t t_esco);
int ull_bredr_esco_stop(struct ull_bredr_esco *esco);
struct ull_bredr_sco *ull_bredr_sco_get(uint8_t idx);
struct ull_bredr_esco *ull_bredr_esco_get(uint8_t idx);
int ull_bredr_lmp_esco_link_req(struct ull_bredr_conn *conn,
				struct pdu_lmp_esco_link_req *params);
int ull_bredr_lmp_remove_esco_link_req(struct ull_bredr_conn *conn,
				       uint8_t esco_handle, uint8_t reason);

/* LMP RX handling */
void ull_bredr_lmp_rx(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);

/* TX data */
int ull_bredr_tx_enqueue(struct ull_bredr_conn *conn, void *pdu, uint16_t len);

/* Internal helpers */
void ull_bredr_conn_setup(struct ull_bredr_conn *conn, uint8_t role,
			  uint8_t *bd_addr, uint8_t lt_addr);
void ull_bredr_conn_cleanup(struct ull_bredr_conn *conn, uint8_t reason);

/* Ticker callbacks */
void ull_bredr_ticker_cb(uint32_t ticks_at_expire, uint32_t ticks_drift,
			 uint32_t remainder, uint16_t lazy, uint8_t force,
			 void *param);

/* Event done handling */
void ull_bredr_done(struct node_rx_event_done *done);

/* RX handling */
void ull_bredr_rx(memq_link_t *link, struct node_rx_pdu **rx);

/*
 * LT Address Management
 * Bluetooth Core Spec Vol 2, Part B - Logical Transport Address
 */
uint8_t ull_bredr_lt_addr_alloc(void);
void ull_bredr_lt_addr_free(uint8_t lt_addr);
bool ull_bredr_lt_addr_reserve(uint8_t lt_addr);

/*
 * AFH Management
 */
void ull_bredr_afh_set(struct ull_bredr_conn *conn, bool start);
void ull_bredr_afh_start(struct ull_bredr_conn *conn);
void ull_bredr_afh_stop_reporting(struct ull_bredr_conn *conn);
void ull_bredr_afh_restore_reporting(struct ull_bredr_conn *conn);
void ull_bredr_afh_peer_ch_class_set(uint8_t link_id, uint8_t *ch_class);
uint8_t *ull_bredr_afh_host_ch_class_get(void);
uint8_t *ull_bredr_afh_master_ch_map_get(void);
void ull_bredr_afh_activate_timer(void);

/*
 * SAM Management
 */
uint16_t ull_bredr_sam_intv_get(uint8_t link_id);
uint16_t ull_bredr_sam_loc_offset_get(uint8_t link_id);
uint16_t ull_bredr_sam_rem_offset_get(uint8_t link_id);
uint16_t ull_bredr_sam_slot_av_get(uint8_t link_id, uint16_t offset_min,
				   uint16_t offset_max);
void ull_bredr_sam_disable(uint8_t link_id);

/*
 * Role Switch
 */
bool ull_bredr_role_switch_start(uint8_t link_id, uint8_t *lt_addr);
void ull_bredr_role_switch_finished(uint8_t link_id, bool success);

/*
 * Connection Sequence
 */
bool ull_bredr_conn_seq_done(struct ull_bredr_conn *conn);
void ull_bredr_conn_complete(struct ull_bredr_conn *conn);
void ull_bredr_conn_complete_evt_send(struct ull_bredr_conn *conn, uint8_t status);

/*
 * LMP PDU Send Functions
 * Bluetooth Core Spec Vol 2, Part C - LMP PDU transmission
 */
void ull_bredr_send_lmp(uint8_t link_id, void *param, uint8_t len);
void ull_bredr_send_pdu_acc(uint8_t idx, uint8_t opcode, uint8_t tr_id);
void ull_bredr_send_pdu_acc_ext4(uint8_t idx, uint8_t opcode, uint8_t tr_id);
void ull_bredr_send_pdu_not_acc(uint8_t idx, uint8_t opcode, uint8_t reason,
				uint8_t tr_id);
void ull_bredr_send_pdu_not_acc_ext4(uint8_t idx, uint8_t opcode, uint8_t reason,
				     uint8_t tr_id);
void ull_bredr_send_pdu_set_afh(uint8_t idx, uint32_t instant, uint8_t mode,
				uint8_t tr_id);
void ull_bredr_send_pdu_au_rand(uint8_t idx, uint8_t *random, uint8_t tr_id);
void ull_bredr_send_pdu_in_rand(uint8_t idx, uint8_t *random, uint8_t tr_id);
void ull_bredr_send_pdu_comb_key(uint8_t idx, uint8_t *random, uint8_t tr_id);
void ull_bredr_send_pdu_max_slot(uint8_t idx, uint8_t max_slot, uint8_t tr_id);
void ull_bredr_send_pdu_max_slot_req(uint8_t idx, uint8_t max_slot, uint8_t tr_id);
void ull_bredr_send_pdu_sres(uint8_t idx, uint8_t *sres, uint8_t tr_id);
void ull_bredr_send_pdu_vers_req(uint8_t idx, uint8_t role);
void ull_bredr_send_pdu_feats_res(uint8_t idx, uint8_t tr_id);
void ull_bredr_send_pdu_feats_ext_req(uint8_t idx, uint8_t page, uint8_t tr_id);
void ull_bredr_send_pdu_setup_cmp(uint8_t idx, uint8_t tr_id);
void ull_bredr_send_pdu_qos_req(uint8_t idx, uint8_t nb_bcst, uint16_t poll_int,
				uint8_t tr_id);
void ull_bredr_send_pdu_lsto(uint8_t idx, uint16_t timeout, uint8_t role);
void ull_bredr_send_pdu_enc_key_sz_req(uint8_t idx, uint8_t key_size, uint8_t tr_id);
void ull_bredr_send_pdu_io_cap_res(uint8_t idx);
void ull_bredr_send_pdu_sp_nb(uint8_t idx, uint8_t *data, uint8_t tr_id);
void ull_bredr_send_pdu_sp_cfm(uint8_t idx, uint8_t *data, uint8_t tr_id);
void ull_bredr_send_pdu_dhkey_chk(uint8_t idx, uint8_t *dhkey, uint8_t tr_id);
void ull_bredr_send_pdu_paus_enc_req(uint8_t idx, uint8_t tr_id);
void ull_bredr_send_pdu_resu_enc_req(uint8_t idx, uint8_t tr_id);
void ull_bredr_send_pdu_auto_rate(uint8_t idx, uint8_t tr_id);
void ull_bredr_send_pdu_ptt_req(uint8_t idx, uint8_t ptt, uint8_t tr_id);

/*
 * Utility Functions
 */
void ull_bredr_util_set_loc_trans_coll(uint8_t link_id, uint8_t opcode,
				       uint8_t opcode_ext, uint8_t mode);
uint8_t ull_bredr_get_nb_acl(uint8_t acl_flag);
bool ull_bredr_is_acl_con(uint8_t link_id);
bool ull_bredr_is_acl_con_role(uint8_t link_id, uint8_t role);

/*
 * Feature Helpers
 */
void ull_bredr_read_features(uint8_t page_nb, uint8_t *page_nb_max,
			     uint8_t *feats);
bool ull_bredr_get_feature(uint8_t *features, uint8_t bit_pos);

/*
 * Authentication/Encryption Helpers
 */
bool ull_bredr_get_auth_en(void);
bool ull_bredr_get_sp_en(void);
bool ull_bredr_get_sec_con_host_supp(void);
uint8_t ull_bredr_get_pin_type(void);
uint16_t ull_bredr_get_connection_accept_timeout(void);
void ull_bredr_get_local_name_seg(uint8_t *name_seg, uint8_t name_offset,
				  uint8_t *name_len);
uint8_t ull_bredr_get_loopback_mode(void);
bool ull_bredr_sp_debug_mode_get(void);
bool ull_bredr_dut_mode_en_get(void);

/*
 * Key Management
 */
void ull_bredr_get_pub_key_192(uint8_t *public_key);
void ull_bredr_get_priv_key_192(uint8_t *private_key);
void ull_bredr_get_pub_key_256(uint8_t *public_key);
void ull_bredr_get_priv_key_256(uint8_t *private_key);
void ull_bredr_get_oob_local_data_192(uint8_t *r, uint8_t *c);
void ull_bredr_get_oob_local_data_256(uint8_t *r, uint8_t *c);

/*
 * LM Environment Access
 */
struct ull_bredr_lm_env *ull_bredr_lm_env_get(void);

/*
 * LMP TX Node (forward declaration for LLL access)
 */
struct lmp_tx_node;

/*
 * LMP TX Queue Functions
 */
struct lmp_tx_node *ull_bredr_lmp_tx_dequeue(struct ull_bredr_conn *conn);
void ull_bredr_lmp_tx_release(struct lmp_tx_node *tx_node);

#endif /* SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_ULL_BREDR_H_ */
