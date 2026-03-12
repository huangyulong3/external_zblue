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
 * BR/EDR Lower Link Layer (LLL) - Baseband Interface
 * Corresponds to Bluetooth Core Spec Vol 2, Part B: Baseband Specification
 */

#ifndef SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_LLL_BREDR_H_
#define SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_LLL_BREDR_H_

#include <zephyr/types.h>

/* Include lll.h only if not already included */
#ifndef SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_LLL_H_
#include "../lll.h"
#endif

#include "pdu_bredr.h"

/*
 * BR/EDR LLL Timing Constants
 */
#define LLL_BREDR_SLOT_US              625
#define LLL_BREDR_HALF_SLOT_US         312
#define LLL_BREDR_CLOCK_WRAP           0x0FFFFFFF  /* 28-bit clock */

/* Inquiry/Page timing */
#define LLL_BREDR_INQUIRY_TRAIN_LEN    16   /* A-train or B-train length */
#define LLL_BREDR_PAGE_TRAIN_LEN       16
#define LLL_BREDR_NPAGE_DEFAULT        128  /* Default Npage */
#define LLL_BREDR_NINQUIRY_DEFAULT     256  /* Default Ninquiry */

/*
 * BR/EDR LLL Event Types
 */
enum lll_bredr_event_type {
	LLL_BREDR_EVENT_NONE = 0,
	LLL_BREDR_EVENT_INQUIRY,
	LLL_BREDR_EVENT_INQUIRY_SCAN,
	LLL_BREDR_EVENT_PAGE,
	LLL_BREDR_EVENT_PAGE_SCAN,
	LLL_BREDR_EVENT_ACL,
	LLL_BREDR_EVENT_SCO,
	LLL_BREDR_EVENT_ESCO,
};

/*
 * BR/EDR LLL Inquiry Context
 */
struct lll_bredr_inquiry {
	struct lll_hdr hdr;

	uint8_t  state;
	uint8_t  lap[3];              /* LAP for inquiry (GIAC/LIAC) */
	uint8_t  inquiry_length;      /* Inquiry duration in 1.28s units */
	uint8_t  num_responses;       /* Max responses (0 = unlimited) */
	uint8_t  current_responses;   /* Current response count */
	uint8_t  train;               /* Current train (A=0, B=1) */
	uint16_t n_inquiry;           /* Inquiry repetitions */
	uint32_t clock_offset;        /* Clock offset for hopping */

	/* Frequency hopping state */
	uint8_t  hop_channel;         /* Current hop channel */
	uint8_t  hop_index;           /* Index in hop sequence */
};

/*
 * BR/EDR LLL Inquiry Scan Context
 */
struct lll_bredr_inquiry_scan {
	struct lll_hdr hdr;

	uint8_t  state;
	uint8_t  lap[3];              /* LAP to respond to */
	uint16_t scan_interval;       /* Scan interval in slots */
	uint16_t scan_window;         /* Scan window in slots */
	uint8_t  interlaced;          /* Interlaced scan mode */

	/* FHS response data */
	uint8_t  fhs_pending;         /* FHS response pending */
	uint8_t  bd_addr[6];          /* Local BD_ADDR */
	uint8_t  class_of_device[3];  /* Class of Device */
};

/*
 * BR/EDR LLL Page Context
 */
struct lll_bredr_page {
	struct lll_hdr hdr;

	uint8_t  state;
	uint8_t  bd_addr[6];          /* Target BD_ADDR */
	uint8_t  page_scan_rep_mode;  /* Page scan repetition mode */
	uint16_t clock_offset;        /* Clock offset hint */
	uint8_t  train;               /* Current train (A=0, B=1) */
	uint16_t n_page;              /* Page repetitions */
	uint16_t page_timeout;        /* Page timeout in slots */

	/* Frequency hopping state */
	uint8_t  hop_channel;
	uint8_t  hop_index;
};

/*
 * BR/EDR LLL Page Scan Context
 */
struct lll_bredr_page_scan {
	struct lll_hdr hdr;

	uint8_t  state;
	uint16_t scan_interval;       /* Scan interval in slots */
	uint16_t scan_window;         /* Scan window in slots */
	uint8_t  interlaced;          /* Interlaced scan mode */

	/* Connection setup */
	uint8_t  id_packet_received;  /* ID packet detected */
	uint8_t  id_received;         /* ID received flag */
	uint8_t  fhs_received;        /* FHS packet received */
	uint8_t  central_bd_addr[6];  /* Central's BD_ADDR */
	uint32_t central_clock;       /* Central's clock */
};

/*
 * BR/EDR LLL ACL Connection Context
 */
struct lll_bredr_conn {
	struct lll_hdr hdr;

	uint8_t  state;
	uint16_t handle;              /* Connection handle */
	uint8_t  role;                /* Central (0) or Peripheral (1) */
	uint8_t  lt_addr;             /* Logical Transport Address */

	/* Peer information */
	uint8_t  bd_addr[6];          /* Peer BD_ADDR */
	uint32_t clock_offset;        /* Clock offset to peer */

	/* Link parameters */
	uint16_t poll_interval;       /* Poll interval (Tpoll) in slots */
	uint16_t supervision_timeout; /* Supervision timeout in slots */
	uint8_t  max_slots;           /* Maximum slots for packets */
	uint8_t  packet_type;         /* Allowed packet types */

	/* Sequence numbers */
	uint8_t  tx_seqn;             /* TX sequence number */
	uint8_t  rx_seqn;             /* Expected RX sequence number */
	uint8_t  tx_arqn;             /* TX acknowledgment */

	/* Flow control */
	uint8_t  flow;                /* Flow control bit */
	uint8_t  flush_timeout;       /* Automatic flush timeout */

	/* Encryption */
	uint8_t  encryption_mode;     /* Current encryption mode */
	uint8_t  encryption_key[16];  /* Encryption key */
	uint8_t  encryption_key_size; /* Key size in bytes */

	/* AFH (Adaptive Frequency Hopping) */
	uint8_t  afh_enabled;         /* AFH enabled */
	uint8_t  afh_channel_map[10]; /* AFH channel map (79 bits) */
	uint8_t  afh_channel_count;   /* Number of used channels */

	/* Power control */
	int8_t   tx_power;            /* Current TX power */
	int8_t   rssi;                /* Last RSSI measurement */

	/* Buffers */
	struct {
		void *head;
		void *tail;
	} memq_tx;                    /* TX queue */

	/* Statistics */
	uint32_t tx_count;
	uint32_t rx_count;
	uint32_t crc_error_count;
};

/*
 * BR/EDR LLL SCO Connection Context
 */
struct lll_bredr_sco {
	struct lll_hdr hdr;

	uint8_t  state;
	uint16_t handle;              /* SCO handle */
	uint8_t  sco_handle;          /* SCO handle (LMP) */
	uint8_t  lt_addr;             /* Logical Transport Address */

	/* Link parameters */
	uint8_t  d_sco;               /* SCO offset */
	uint8_t  t_sco;               /* SCO interval */
	uint8_t  packet_type;         /* SCO packet type */
	uint8_t  air_mode;            /* Air mode (CVSD, etc.) */
	uint16_t voice_setting;       /* Voice setting */

	/* Role */
	uint8_t  role;                /* Central (0) or Peripheral (1) */

	/* Peer information */
	uint8_t  bd_addr[6];          /* Peer BD_ADDR */

	/* Parent ACL connection */
	uint16_t acl_handle;          /* Parent ACL handle */

	/* Statistics */
	uint32_t rx_count;
	uint32_t err_count;
};

/*
 * BR/EDR LLL eSCO Connection Context
 */
struct lll_bredr_esco {
	struct lll_hdr hdr;

	uint8_t  state;
	uint16_t handle;              /* eSCO handle */
	uint8_t  esco_handle;         /* eSCO handle (LMP) */
	uint8_t  esco_lt_addr;        /* eSCO LT_ADDR */
	uint8_t  lt_addr;             /* Logical Transport Address */

	/* Link parameters */
	uint8_t  d_esco;              /* eSCO offset */
	uint8_t  t_esco;              /* eSCO interval */
	uint8_t  w_esco;              /* eSCO window */
	uint8_t  packet_type_m2s;     /* M->S packet type */
	uint8_t  packet_type_s2m;     /* S->M packet type */
	uint8_t  packet_type;         /* Current packet type */
	uint16_t packet_length_m2s;
	uint16_t packet_length_s2m;
	uint8_t  air_mode;            /* Air mode */
	uint8_t  retx_effort;         /* Retransmission effort */
	uint16_t voice_setting;       /* Voice setting */

	/* Role */
	uint8_t  role;                /* Central (0) or Peripheral (1) */

	/* Peer information */
	uint8_t  bd_addr[6];          /* Peer BD_ADDR */

	/* Parent ACL connection */
	uint16_t acl_handle;          /* Parent ACL handle */

	/* Retransmission state */
	uint8_t  retx_count;          /* Current retransmission count */

	/* Statistics */
	uint32_t rx_count;
	uint32_t err_count;
};

/*
 * BR/EDR LLL Function Declarations
 */

/* Initialization */
int lll_bredr_init(void);
int lll_bredr_reset(void);

/* Inquiry operations */
int lll_bredr_inquiry_start(struct lll_bredr_inquiry *inquiry);
int lll_bredr_inquiry_stop(struct lll_bredr_inquiry *inquiry);
void lll_bredr_inquiry_prepare(void *param);
void lll_bredr_inquiry_isr(void *param);

/* Inquiry scan operations */
int lll_bredr_inquiry_scan_start(struct lll_bredr_inquiry_scan *scan);
int lll_bredr_inquiry_scan_stop(struct lll_bredr_inquiry_scan *scan);
void lll_bredr_inquiry_scan_prepare(void *param);
void lll_bredr_inquiry_scan_isr(void *param);

/* Page operations */
int lll_bredr_page_start(struct lll_bredr_page *page);
int lll_bredr_page_stop(struct lll_bredr_page *page);
void lll_bredr_page_prepare(void *param);
void lll_bredr_page_isr(void *param);

/* Page scan operations */
int lll_bredr_page_scan_start(struct lll_bredr_page_scan *scan);
int lll_bredr_page_scan_stop(struct lll_bredr_page_scan *scan);
void lll_bredr_page_scan_prepare(void *param);
void lll_bredr_page_scan_isr(void *param);

/* ACL connection operations */
int lll_bredr_conn_start(struct lll_bredr_conn *conn);
int lll_bredr_conn_stop(struct lll_bredr_conn *conn);
void lll_bredr_conn_prepare(void *param);
void lll_bredr_conn_isr(void *param);

/* SCO operations */
int lll_bredr_sco_start(struct lll_bredr_sco *sco);
int lll_bredr_sco_stop(struct lll_bredr_sco *sco);
void lll_bredr_sco_prepare(void *param);
void lll_bredr_sco_isr(void *param);

/* eSCO operations */
int lll_bredr_esco_start(struct lll_bredr_esco *esco);
int lll_bredr_esco_stop(struct lll_bredr_esco *esco);
void lll_bredr_esco_prepare(void *param);
void lll_bredr_esco_isr(void *param);

/* Frequency hopping */
uint8_t lll_bredr_hop_channel_calc(uint32_t clock, uint8_t *channel_map,
				   uint8_t channel_count, uint32_t uap_lap);
uint8_t lll_bredr_afh_channel_calc(uint32_t clock, uint8_t *afh_map,
				   uint8_t afh_channel_count, uint32_t uap_lap);

/* Access code generation */
void lll_bredr_access_code_gen(uint32_t lap, uint8_t *access_code);
void lll_bredr_dac_gen(uint8_t *bd_addr, uint8_t *access_code);
void lll_bredr_cac_gen(uint8_t *bd_addr, uint8_t *access_code);

/* Packet handling */
int lll_bredr_tx_pdu(struct lll_bredr_conn *conn, void *pdu, uint8_t len);
int lll_bredr_rx_pdu(struct lll_bredr_conn *conn, void *pdu, uint8_t *len);

/* Encryption */
int lll_bredr_encrypt_start(struct lll_bredr_conn *conn, uint8_t *key,
			    uint8_t key_size);
int lll_bredr_encrypt_stop(struct lll_bredr_conn *conn);

#endif /* SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_LLL_BREDR_H_ */
