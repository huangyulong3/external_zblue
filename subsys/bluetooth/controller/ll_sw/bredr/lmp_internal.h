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
 * BR/EDR LMP Internal Definitions
 * Based on BLE LLCP architecture (ull_llcp_internal.h)
 */

#ifndef SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_LMP_INTERNAL_H_
#define SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_LMP_INTERNAL_H_

#include <zephyr/types.h>
#include <zephyr/sys/slist.h>
#include "pdu_bredr.h"

/*
 * LMP TX Node Structure
 * Used for LMP PDU transmission queue
 */
struct lmp_tx_node {
	sys_snode_t node;
	uint8_t link_id;
	uint8_t len;
	uint8_t pdu[17];  /* Max LMP PDU size */
};

/*
 * LMP Memory Pool Descriptor
 */
struct lmp_mem_pool {
	void *free;
	uint8_t *pool;
};

/*
 * LMP Procedure Types
 * Bluetooth Core Spec Vol 2, Part C - LMP Procedures
 */
enum lmp_proc {
	LMP_PROC_UNKNOWN = 0,
	/* Connection Setup */
	LMP_PROC_VERSION_EXCHANGE,
	LMP_PROC_FEATURES_EXCHANGE,
	LMP_PROC_EXT_FEATURES_EXCHANGE,
	LMP_PROC_NAME_REQUEST,
	LMP_PROC_CLOCK_OFFSET,
	LMP_PROC_SETUP_COMPLETE,
	/* Authentication */
	LMP_PROC_AUTHENTICATION,
	LMP_PROC_PAIRING,
	/* Encryption */
	LMP_PROC_ENCRYPTION_START,
	LMP_PROC_ENCRYPTION_STOP,
	LMP_PROC_ENCRYPTION_PAUSE,
	LMP_PROC_ENCRYPTION_RESUME,
	/* Secure Simple Pairing */
	LMP_PROC_SSP_IO_CAP,
	LMP_PROC_SSP_PUBLIC_KEY,
	LMP_PROC_SSP_NUMERIC_COMP,
	LMP_PROC_SSP_PASSKEY,
	LMP_PROC_SSP_OOB,
	LMP_PROC_SSP_DHKEY_CHECK,
	/* Power Modes */
	LMP_PROC_HOLD,
	LMP_PROC_SNIFF,
	LMP_PROC_UNSNIFF,
	LMP_PROC_PARK,
	LMP_PROC_UNPARK,
	LMP_PROC_SNIFF_SUBRATING,
	/* Link Control */
	LMP_PROC_ROLE_SWITCH,
	LMP_PROC_QOS,
	LMP_PROC_MAX_SLOTS,
	LMP_PROC_SUPERVISION_TIMEOUT,
	LMP_PROC_TIMING_ACCURACY,
	LMP_PROC_POWER_CONTROL,
	/* AFH */
	LMP_PROC_AFH_SET,
	LMP_PROC_AFH_CLASSIFICATION,
	/* SCO/eSCO */
	LMP_PROC_SCO_SETUP,
	LMP_PROC_SCO_REMOVE,
	LMP_PROC_ESCO_SETUP,
	LMP_PROC_ESCO_MODIFY,
	LMP_PROC_ESCO_REMOVE,
	/* Packet Type */
	LMP_PROC_PACKET_TYPE_TABLE,
	/* Termination */
	LMP_PROC_DETACH,
	/* Ping */
	LMP_PROC_PING,
	/* Helper enum */
	LMP_PROC_NONE = 0xFF,
};

/*
 * Generic IDLE state for all procedures
 */
enum lmp_proc_state_idle {
	LMP_STATE_IDLE = 0,
};

/*
 * Wait reasons for TX buffer allocation
 */
enum lmp_wait_reason {
	LMP_WAITING_FOR_NOTHING = 0,
	LMP_WAITING_FOR_TX_BUFFER,
};

/*
 * Procedure Incompatibility (collision handling)
 */
enum lmp_proc_incompat {
	LMP_INCOMPAT_NO_COLLISION,
	LMP_INCOMPAT_RESOLVABLE,
	LMP_INCOMPAT_RESERVED,
};

/*
 * Invalid LMP Opcode
 */
#define LMP_INVALID_OPCODE (0xFFU)

/*
 * LMP Procedure Context
 * Similar to BLE's proc_ctx structure
 */
struct lmp_proc_ctx {
	/* Must be first for sys_slist to work */
	sys_snode_t node;

	/* Memory pool owner */
	struct lmp_mem_pool *owner;

	/* Wait list node for TX buffer allocation */
	sys_snode_t wait_node;

	/* Procedure type */
	enum lmp_proc proc;

	/* Expected response opcode */
	uint8_t response_opcode;

	/* Procedure FSM state */
	uint8_t state;

	/* Expected RX opcode */
	uint8_t rx_opcode;

	/* Last transmitted opcode */
	uint8_t tx_opcode;

	/* Transaction ID */
	uint8_t tid;

	/* Done flag - safe to release context */
	uint8_t done;

	/* Wait reason */
	enum lmp_wait_reason wait_reason;

	/* Node references */
	struct {
		/* TX node awaiting ack */
		struct node_tx *tx_ack;
		/* Pre-allocated TX node */
		struct node_tx *tx;
	} node_ref;

	/* Procedure-specific data */
	union {
		/* Version Exchange */
		struct {
			uint8_t vers_nr;
			uint16_t comp_id;
			uint16_t sub_vers_nr;
		} version;

		/* Features Exchange */
		struct {
			uint8_t page;
			uint8_t max_page;
			uint8_t features[8];
		} features;

		/* Name Request */
		struct {
			uint8_t offset;
			uint8_t length;
			uint8_t name_frag[14];
		} name;

		/* Authentication */
		struct {
			uint8_t au_rand[16];
			uint8_t sres[4];
			uint8_t aco[12];
		} auth;

		/* Encryption */
		struct {
			uint8_t mode;
			uint8_t key_size;
			uint8_t en_rand[16];
		} enc;

		/* SSP */
		struct {
			uint8_t io_cap;
			uint8_t oob_data;
			uint8_t auth_req;
			uint8_t public_key[64];
			uint8_t nonce[16];
			uint8_t commitment[16];
			uint8_t dhkey_check[16];
			uint32_t passkey;
			uint8_t passkey_bit;
		} ssp;

		/* Power Modes */
		struct {
			uint16_t interval;
			uint16_t attempt;
			uint16_t timeout;
			uint32_t instant;
		} power;

		/* Role Switch */
		struct {
			uint32_t instant;
			uint8_t lt_addr;
		} rsw;

		/* QoS */
		struct {
			uint16_t poll_interval;
			uint8_t nbc;
		} qos;

		/* AFH */
		struct {
			uint32_t instant;
			uint8_t mode;
			uint8_t channel_map[10];
		} afh;

		/* SCO */
		struct {
			uint8_t sco_handle;
			uint8_t d_sco;
			uint8_t t_sco;
			uint8_t packet_type;
			uint8_t air_mode;
		} sco;

		/* eSCO */
		struct {
			uint8_t esco_handle;
			uint8_t esco_lt_addr;
			uint8_t timing_control_flags;
			uint8_t d_esco;
			uint8_t t_esco;
			uint8_t w_esco;
			uint8_t packet_type_m_to_s;
			uint8_t packet_type_s_to_m;
			uint16_t packet_length_m_to_s;
			uint16_t packet_length_s_to_m;
			uint8_t air_mode;
			uint8_t negotiation_state;
		} esco;

		/* Detach */
		struct {
			uint8_t error_code;
		} detach;

		/* Max Slots */
		struct {
			uint8_t max_slots;
		} max_slot;

		/* Supervision Timeout */
		struct {
			uint16_t timeout;
		} lsto;
	} data;

	/* Unknown response info */
	struct {
		uint8_t opcode;
	} unknown_response;

	/* Reject info */
	struct {
		uint8_t reject_opcode;
		uint8_t error_code;
	} reject;
};

/*
 * LMP Connection LLCP-like State
 */
struct lmp_conn_state {
	/* Local procedure request FSM */
	struct {
		sys_slist_t pend_proc_list;
		uint16_t prt_expire;  /* Procedure Response Timeout */
		uint8_t pause;
	} local;

	/* Remote procedure request FSM */
	struct {
		sys_slist_t pend_proc_list;
		uint16_t prt_expire;
		uint8_t pause;
		enum lmp_proc_incompat incompat;
		uint8_t collision;
	} remote;

	/* Procedure Response Timeout reload value */
	uint16_t prt_reload;

	/* TX buffer allocation count */
	uint8_t tx_buffer_alloc;

	/* TX node release list */
	struct node_tx *tx_node_release;
};

/*
 * LMP Resource Management
 */
void lmp_init(void);
struct lmp_proc_ctx *lmp_create_local_procedure(enum lmp_proc proc);
struct lmp_proc_ctx *lmp_create_remote_procedure(enum lmp_proc proc);
void lmp_proc_ctx_release(struct lmp_proc_ctx *ctx);

/*
 * TX Buffer Management
 */
bool lmp_tx_alloc_peek(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx);
void lmp_tx_alloc_unpeek(struct lmp_proc_ctx *ctx);
struct node_tx *lmp_tx_alloc(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx);
void lmp_tx_release(struct ull_bredr_conn *conn, struct node_tx *tx);

/*
 * TX Queue Management
 */
void lmp_tx_enqueue(struct ull_bredr_conn *conn, struct node_tx *tx);
void lmp_tx_pause_data(struct ull_bredr_conn *conn);
void lmp_tx_resume_data(struct ull_bredr_conn *conn);

/*
 * Procedure Response Timeout
 */
void lmp_lr_prt_restart(struct ull_bredr_conn *conn);
void lmp_lr_prt_stop(struct ull_bredr_conn *conn);
void lmp_rr_prt_restart(struct ull_bredr_conn *conn);
void lmp_rr_prt_stop(struct ull_bredr_conn *conn);

/*
 * Local Request FSM
 */
struct lmp_proc_ctx *lmp_lr_peek(struct ull_bredr_conn *conn);
bool lmp_lr_ispaused(struct ull_bredr_conn *conn);
void lmp_lr_pause(struct ull_bredr_conn *conn);
void lmp_lr_resume(struct ull_bredr_conn *conn);
void lmp_lr_enqueue(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx);
void lmp_lr_init(struct ull_bredr_conn *conn);
void lmp_lr_run(struct ull_bredr_conn *conn);
void lmp_lr_complete(struct ull_bredr_conn *conn);
void lmp_lr_rx(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx,
	       uint8_t *pdu, uint8_t len);
void lmp_lr_tx_ack(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx,
		   struct node_tx *tx);

/*
 * Remote Request FSM
 */
struct lmp_proc_ctx *lmp_rr_peek(struct ull_bredr_conn *conn);
bool lmp_rr_ispaused(struct ull_bredr_conn *conn);
void lmp_rr_pause(struct ull_bredr_conn *conn);
void lmp_rr_resume(struct ull_bredr_conn *conn);
void lmp_rr_init(struct ull_bredr_conn *conn);
void lmp_rr_run(struct ull_bredr_conn *conn);
void lmp_rr_complete(struct ull_bredr_conn *conn);
void lmp_rr_new(struct ull_bredr_conn *conn, uint8_t *pdu, uint8_t len);
void lmp_rr_rx(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx,
	       uint8_t *pdu, uint8_t len);
void lmp_rr_tx_ack(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx,
		   struct node_tx *tx);

/*
 * Local Procedure Handlers
 */
void lmp_lp_comm_init_proc(struct lmp_proc_ctx *ctx);
void lmp_lp_comm_run(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx);
void lmp_lp_comm_rx(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx,
		    uint8_t *pdu, uint8_t len);
void lmp_lp_comm_tx_ack(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx,
			struct node_tx *tx);

void lmp_lp_enc_init_proc(struct lmp_proc_ctx *ctx);
void lmp_lp_enc_run(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx);
void lmp_lp_enc_rx(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx,
		   uint8_t *pdu, uint8_t len);

void lmp_lp_ssp_init_proc(struct lmp_proc_ctx *ctx);
void lmp_lp_ssp_run(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx);
void lmp_lp_ssp_rx(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx,
		   uint8_t *pdu, uint8_t len);

/*
 * Remote Procedure Handlers
 */
void lmp_rp_comm_init_proc(struct lmp_proc_ctx *ctx);
void lmp_rp_comm_run(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx);
void lmp_rp_comm_rx(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx,
		    uint8_t *pdu, uint8_t len);
void lmp_rp_comm_tx_ack(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx,
			struct node_tx *tx);

void lmp_rp_enc_init_proc(struct lmp_proc_ctx *ctx);
void lmp_rp_enc_run(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx);
void lmp_rp_enc_rx(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx,
		   uint8_t *pdu, uint8_t len);

void lmp_rp_ssp_init_proc(struct lmp_proc_ctx *ctx);
void lmp_rp_ssp_run(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx);
void lmp_rp_ssp_rx(struct ull_bredr_conn *conn, struct lmp_proc_ctx *ctx,
		   uint8_t *pdu, uint8_t len);

/*
 * PDU Encode/Decode Helpers
 */
void lmp_pdu_encode_version_req(struct lmp_proc_ctx *ctx, struct pdu_lmp_version *pdu);
void lmp_pdu_encode_version_res(struct lmp_proc_ctx *ctx, struct pdu_lmp_version *pdu);
void lmp_pdu_decode_version(struct lmp_proc_ctx *ctx, struct pdu_lmp_version *pdu);

void lmp_pdu_encode_features_req(struct lmp_proc_ctx *ctx, struct pdu_lmp_features *pdu);
void lmp_pdu_encode_features_res(struct lmp_proc_ctx *ctx, struct pdu_lmp_features *pdu);
void lmp_pdu_decode_features(struct lmp_proc_ctx *ctx, struct pdu_lmp_features *pdu);

void lmp_pdu_encode_accepted(uint8_t opcode, uint8_t tid, struct pdu_lmp_accepted *pdu);
void lmp_pdu_encode_not_accepted(uint8_t opcode, uint8_t error, uint8_t tid,
				 struct pdu_lmp_not_accepted *pdu);

void lmp_pdu_encode_detach(uint8_t reason, struct pdu_lmp_detach *pdu);

/*
 * Connection State Management
 */
void lmp_conn_init(struct ull_bredr_conn *conn);
void lmp_conn_reset(struct ull_bredr_conn *conn);
void lmp_run(struct ull_bredr_conn *conn);
int lmp_prt_elapse(struct ull_bredr_conn *conn, uint16_t elapsed_event,
		   uint8_t *error_code);

/*
 * Public API
 */
uint8_t lmp_version_exchange(struct ull_bredr_conn *conn);
uint8_t lmp_features_exchange(struct ull_bredr_conn *conn);
uint8_t lmp_name_request(struct ull_bredr_conn *conn);
uint8_t lmp_detach(struct ull_bredr_conn *conn, uint8_t error_code);
uint8_t lmp_authentication(struct ull_bredr_conn *conn);
uint8_t lmp_encryption_start(struct ull_bredr_conn *conn);
uint8_t lmp_encryption_stop(struct ull_bredr_conn *conn);
uint8_t lmp_ssp_initiate(struct ull_bredr_conn *conn);
uint8_t lmp_role_switch(struct ull_bredr_conn *conn);
uint8_t lmp_sniff_mode(struct ull_bredr_conn *conn, uint16_t interval,
		       uint16_t attempt, uint16_t timeout);
uint8_t lmp_exit_sniff_mode(struct ull_bredr_conn *conn);
uint8_t lmp_qos_setup(struct ull_bredr_conn *conn, uint16_t poll_interval);
uint8_t lmp_afh_set(struct ull_bredr_conn *conn, uint8_t *channel_map);

#endif /* SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_LMP_INTERNAL_H_ */
