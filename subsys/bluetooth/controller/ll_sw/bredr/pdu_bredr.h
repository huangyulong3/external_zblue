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
 * BR/EDR PDU definitions based on Bluetooth Core Spec Vol 2
 * Part B: Baseband Specification
 * Part C: Link Manager Protocol Specification
 */

#ifndef SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_PDU_BREDR_H_
#define SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_PDU_BREDR_H_

#include <zephyr/types.h>

/*
 * BR/EDR Physical Channel Parameters
 * Core Spec Vol 2, Part B, Section 1
 */
#define BREDR_CHANNEL_COUNT          79    /* 2402 + k MHz, k = 0..78 */
#define BREDR_SLOT_US                625   /* Time slot duration in microseconds */
#define BREDR_CLOCK_TICK_US          312   /* Half slot, 312.5 us (LSB of clock) */
#define BREDR_TPOLL_DEFAULT          40    /* Default poll interval in slots */

/* BR/EDR Data Rates */
#define BREDR_RATE_BR_1MBPS          1     /* Basic Rate: 1 Mbps */
#define BREDR_RATE_EDR_2MBPS         2     /* Enhanced Data Rate: 2 Mbps */
#define BREDR_RATE_EDR_3MBPS         3     /* Enhanced Data Rate: 3 Mbps */

/*
 * BR/EDR Packet Types
 * Core Spec Vol 2, Part B, Section 6
 */

/* Common packet types (used in both SCO and ACL) */
#define BREDR_PKT_TYPE_NULL          0x00  /* No payload */
#define BREDR_PKT_TYPE_POLL          0x01  /* No payload, requires response */
#define BREDR_PKT_TYPE_FHS           0x02  /* FHS packet */
#define BREDR_PKT_TYPE_DM1           0x03  /* DM1 packet (ACL) */

/* ACL packet types */
#define BREDR_PKT_TYPE_DH1           0x04  /* DH1 packet */
#define BREDR_PKT_TYPE_DM3           0x0A  /* DM3 packet */
#define BREDR_PKT_TYPE_DH3           0x0B  /* DH3 packet */
#define BREDR_PKT_TYPE_DM5           0x0E  /* DM5 packet */
#define BREDR_PKT_TYPE_DH5           0x0F  /* DH5 packet */

/* EDR ACL packet types (2 Mbps and 3 Mbps) */
#define BREDR_PKT_TYPE_2DH1          0x04  /* 2-DH1 packet (EDR) */
#define BREDR_PKT_TYPE_2DH3          0x0A  /* 2-DH3 packet (EDR) */
#define BREDR_PKT_TYPE_2DH5          0x0E  /* 2-DH5 packet (EDR) */
#define BREDR_PKT_TYPE_3DH1          0x08  /* 3-DH1 packet (EDR) */
#define BREDR_PKT_TYPE_3DH3          0x0B  /* 3-DH3 packet (EDR) */
#define BREDR_PKT_TYPE_3DH5          0x0F  /* 3-DH5 packet (EDR) */

/* SCO packet types */
#define BREDR_PKT_TYPE_HV1           0x05  /* HV1 packet */
#define BREDR_PKT_TYPE_HV2           0x06  /* HV2 packet */
#define BREDR_PKT_TYPE_HV3           0x07  /* HV3 packet */
#define BREDR_PKT_TYPE_DV            0x08  /* DV packet */

/* eSCO packet types */
#define BREDR_PKT_TYPE_EV3           0x07  /* EV3 packet */
#define BREDR_PKT_TYPE_EV4           0x0C  /* EV4 packet */
#define BREDR_PKT_TYPE_EV5           0x0D  /* EV5 packet */
#define BREDR_PKT_TYPE_2EV3          0x06  /* 2-EV3 packet (EDR) */
#define BREDR_PKT_TYPE_2EV5          0x0C  /* 2-EV5 packet (EDR) */
#define BREDR_PKT_TYPE_3EV3          0x07  /* 3-EV3 packet (EDR) */
#define BREDR_PKT_TYPE_3EV5          0x0D  /* 3-EV5 packet (EDR) */

/*
 * BR/EDR Packet Maximum Payload Sizes (bytes)
 * Core Spec Vol 2, Part B, Section 6.5
 */
#define BREDR_DM1_MAX_PAYLOAD        17
#define BREDR_DH1_MAX_PAYLOAD        27
#define BREDR_DM3_MAX_PAYLOAD        121
#define BREDR_DH3_MAX_PAYLOAD        183
#define BREDR_DM5_MAX_PAYLOAD        224
#define BREDR_DH5_MAX_PAYLOAD        339

/* EDR packet payload sizes */
#define BREDR_2DH1_MAX_PAYLOAD       54
#define BREDR_2DH3_MAX_PAYLOAD       367
#define BREDR_2DH5_MAX_PAYLOAD       679
#define BREDR_3DH1_MAX_PAYLOAD       83
#define BREDR_3DH3_MAX_PAYLOAD       552
#define BREDR_3DH5_MAX_PAYLOAD       1021

/* SCO packet payload sizes */
#define BREDR_HV1_PAYLOAD            10
#define BREDR_HV2_PAYLOAD            20
#define BREDR_HV3_PAYLOAD            30

/*
 * Access Code Types
 * Core Spec Vol 2, Part B, Section 6.3
 */
#define BREDR_ACCESS_CODE_CAC        0     /* Channel Access Code */
#define BREDR_ACCESS_CODE_DAC        1     /* Device Access Code */
#define BREDR_ACCESS_CODE_IAC        2     /* Inquiry Access Code */

/* General Inquiry Access Code (GIAC) LAP */
#define BREDR_GIAC_LAP               0x9E8B33
/* Limited Inquiry Access Code (LIAC) LAP */
#define BREDR_LIAC_LAP               0x9E8B00

/*
 * Logical Transport Address (LT_ADDR)
 * Core Spec Vol 2, Part B, Section 4.4
 */
#define BREDR_LT_ADDR_BROADCAST      0     /* Broadcast address */
#define BREDR_LT_ADDR_MAX            7     /* Maximum LT_ADDR value */

/*
 * BR/EDR Packet Header
 * Core Spec Vol 2, Part B, Section 6.4
 */
struct pdu_bredr_header {
#ifdef CONFIG_LITTLE_ENDIAN
	uint8_t lt_addr:3;    /* Logical Transport Address */
	uint8_t type:4;       /* Packet type */
	uint8_t flow:1;       /* Flow control bit */
	uint8_t arqn:1;       /* Acknowledgment indication */
	uint8_t seqn:1;       /* Sequence number */
	uint8_t rfu:6;        /* Reserved for future use */
	uint8_t hec;          /* Header Error Check */
#else
	uint8_t flow:1;
	uint8_t type:4;
	uint8_t lt_addr:3;
	uint8_t rfu:6;
	uint8_t seqn:1;
	uint8_t arqn:1;
	uint8_t hec;
#endif
} __packed;

/*
 * FHS Packet Payload
 * Core Spec Vol 2, Part B, Section 6.5.1.4
 */
struct pdu_bredr_fhs {
	uint8_t  parity[3];           /* Parity bits (34 bits, lower 24) */
#ifdef CONFIG_LITTLE_ENDIAN
	uint8_t  parity_high:2;       /* Parity bits (upper 10 bits, lower 2) */
	uint8_t  lap_low:6;           /* LAP lower 6 bits */
#else
	uint8_t  lap_low:6;
	uint8_t  parity_high:2;
#endif
	uint8_t  lap_mid;             /* LAP middle 8 bits */
#ifdef CONFIG_LITTLE_ENDIAN
	uint8_t  lap_high:2;          /* LAP upper 2 bits */
	uint8_t  eir:1;               /* Extended Inquiry Response */
	uint8_t  rfu:1;               /* Reserved */
	uint8_t  sr:2;                /* Scan Repetition */
	uint8_t  sp:2;                /* Reserved (SP mode) */
#else
	uint8_t  sp:2;
	uint8_t  sr:2;
	uint8_t  rfu:1;
	uint8_t  eir:1;
	uint8_t  lap_high:2;
#endif
	uint8_t  uap;                 /* Upper Address Part */
	uint8_t  nap[2];              /* Non-significant Address Part */
	uint8_t  class_of_device[3];  /* Class of Device */
	uint8_t  lt_addr:3;           /* LT_ADDR assigned to recipient */
	uint8_t  clk_low:5;           /* CLK[2:6] */
	uint8_t  clk_mid[2];          /* CLK[7:22] */
#ifdef CONFIG_LITTLE_ENDIAN
	uint8_t  clk_high:4;          /* CLK[23:26] */
	uint8_t  page_scan_mode:3;    /* Page Scan Mode */
	uint8_t  rfu2:1;              /* Reserved */
#else
	uint8_t  rfu2:1;
	uint8_t  page_scan_mode:3;
	uint8_t  clk_high:4;
#endif
} __packed;

/*
 * ACL Payload Header (L2CAP)
 * Core Spec Vol 2, Part B, Section 6.6
 */
struct pdu_bredr_acl_header {
#ifdef CONFIG_LITTLE_ENDIAN
	uint8_t  llid:2;              /* Logical Link ID */
	uint8_t  flow:1;              /* Flow bit */
	uint8_t  length_low:5;        /* Length lower 5 bits */
	uint8_t  length_high:5;       /* Length upper 5 bits */
	uint8_t  rfu:3;               /* Reserved */
#else
	uint8_t  length_low:5;
	uint8_t  flow:1;
	uint8_t  llid:2;
	uint8_t  rfu:3;
	uint8_t  length_high:5;
#endif
} __packed;

/* ACL Logical Link IDs */
#define BREDR_LLID_CONTINUATION      0x01  /* Continuation fragment */
#define BREDR_LLID_START             0x02  /* Start of L2CAP PDU */
#define BREDR_LLID_LMP               0x03  /* LMP message */

/*
 * LMP PDU Opcodes
 * Core Spec Vol 2, Part C, Section 5
 */

/* Single-byte opcodes (0x01 - 0x7B) */
#define LMP_NAME_REQ                 0x01
#define LMP_NAME_RES                 0x02
#define LMP_ACCEPTED                 0x03
#define LMP_NOT_ACCEPTED             0x04
#define LMP_CLKOFFSET_REQ            0x05
#define LMP_CLKOFFSET_RES            0x06
#define LMP_DETACH                   0x07
#define LMP_IN_RAND                  0x08
#define LMP_COMB_KEY                 0x09
#define LMP_UNIT_KEY                 0x0A
#define LMP_AU_RAND                  0x0B
#define LMP_SRES                     0x0C
#define LMP_TEMP_RAND                0x0D
#define LMP_TEMP_KEY                 0x0E
#define LMP_ENCRYPTION_MODE_REQ      0x0F
#define LMP_ENCRYPTION_KEY_SIZE_REQ  0x10
#define LMP_START_ENCRYPTION_REQ     0x11
#define LMP_STOP_ENCRYPTION_REQ      0x12
#define LMP_SWITCH_REQ               0x13
#define LMP_HOLD                     0x14
#define LMP_HOLD_REQ                 0x15
#define LMP_SNIFF_REQ                0x17
#define LMP_UNSNIFF_REQ              0x18
#define LMP_PARK_REQ                 0x19
#define LMP_SET_BROADCAST_SCAN_WINDOW 0x1B
#define LMP_MODIFY_BEACON            0x1C
#define LMP_UNPARK_BD_ADDR_REQ       0x1D
#define LMP_UNPARK_PM_ADDR_REQ       0x1E
#define LMP_INCR_POWER_REQ           0x1F
#define LMP_DECR_POWER_REQ           0x20
#define LMP_MAX_POWER                0x21
#define LMP_MIN_POWER                0x22
#define LMP_AUTO_RATE                0x23
#define LMP_PREFERRED_RATE           0x24
#define LMP_VERSION_REQ              0x25
#define LMP_VERSION_RES              0x26
#define LMP_FEATURES_REQ             0x27
#define LMP_FEATURES_RES             0x28
#define LMP_QUALITY_OF_SERVICE       0x29
#define LMP_QUALITY_OF_SERVICE_REQ   0x2A
#define LMP_SCO_LINK_REQ             0x2B
#define LMP_REMOVE_SCO_LINK_REQ      0x2C
#define LMP_MAX_SLOT                 0x2D
#define LMP_MAX_SLOT_REQ             0x2E
#define LMP_TIMING_ACCURACY_REQ      0x2F
#define LMP_TIMING_ACCURACY_RES      0x30
#define LMP_SETUP_COMPLETE           0x31
#define LMP_USE_SEMI_PERMANENT_KEY   0x32
#define LMP_HOST_CONNECTION_REQ      0x33
#define LMP_SLOT_OFFSET              0x34
#define LMP_PAGE_MODE_REQ            0x35
#define LMP_PAGE_SCAN_MODE_REQ       0x36
#define LMP_SUPERVISION_TIMEOUT      0x37
#define LMP_TEST_ACTIVATE            0x38
#define LMP_TEST_CONTROL             0x39
#define LMP_ENCRYPTION_KEY_SIZE_MASK_REQ 0x3A
#define LMP_ENCRYPTION_KEY_SIZE_MASK_RES 0x3B
#define LMP_SET_AFH                  0x3C
#define LMP_ENCAPSULATED_HEADER      0x3D
#define LMP_ENCAPSULATED_PAYLOAD     0x3E
#define LMP_SIMPLE_PAIRING_CONFIRM   0x3F
#define LMP_SIMPLE_PAIRING_NUMBER    0x40
#define LMP_DHKEY_CHECK              0x41
#define LMP_PAUSE_ENCRYPTION_AES_REQ 0x42

/* Aliases for Simple Pairing opcodes */
#define LMP_SP_CONFIRM               LMP_SIMPLE_PAIRING_CONFIRM
#define LMP_SP_NUMBER                LMP_SIMPLE_PAIRING_NUMBER

/* Extended opcodes (escape code 0x7C-0x7F + extended opcode) */
#define LMP_ESCAPE_1                 0x7C
#define LMP_ESCAPE_2                 0x7D
#define LMP_ESCAPE_3                 0x7E
#define LMP_ESCAPE_4                 0x7F

/* Extended opcodes (after escape 0x7F) */
#define LMP_EXT_ACCEPTED             0x01
#define LMP_EXT_NOT_ACCEPTED         0x02
#define LMP_EXT_FEATURES_REQ         0x03
#define LMP_EXT_FEATURES_RES         0x04
#define LMP_EXT_CLK_ADJ              0x05
#define LMP_EXT_CLK_ADJ_ACK          0x06
#define LMP_EXT_CLK_ADJ_REQ          0x07
#define LMP_EXT_PACKET_TYPE_TABLE_REQ 0x0B
#define LMP_EXT_ESCO_LINK_REQ        0x0C
#define LMP_EXT_REMOVE_ESCO_LINK_REQ 0x0D
#define LMP_EXT_CHANNEL_CLASS_REQ    0x10
#define LMP_EXT_CHANNEL_CLASS        0x11
#define LMP_EXT_SNIFF_SUBRATING_REQ  0x15
#define LMP_EXT_SNIFF_SUBRATING_RES  0x16
#define LMP_EXT_SSR_REQ              0x15
#define LMP_EXT_SSR_RES              0x16
#define LMP_EXT_PAUSE_ENCRYPTION_REQ 0x17
#define LMP_EXT_RESUME_ENCRYPTION_REQ 0x18
#define LMP_EXT_IO_CAPABILITY_REQ    0x19
#define LMP_EXT_IO_CAPABILITY_RES    0x1A
#define LMP_EXT_NUMERIC_COMPARISON_FAILED 0x1B
#define LMP_EXT_PASSKEY_FAILED       0x1C
#define LMP_EXT_OOB_FAILED           0x1D
#define LMP_EXT_KEYPRESS_NOTIFICATION 0x1E
#define LMP_EXT_POWER_CONTROL_REQ    0x1F
#define LMP_EXT_POWER_CONTROL_RES    0x20
#define LMP_EXT_PING_REQ             0x21
#define LMP_EXT_PING_RES             0x22
#define LMP_EXT_CHANNEL_CLASSIFICATION_REQ 0x10  /* Alias for channel class req */

/* Legacy LMP power control opcodes (non-extended) */
#define LMP_POWER_CTRL_REQ           LMP_INCR_POWER_REQ
#define LMP_POWER_CTRL_RES           LMP_MAX_POWER

/* AES encryption pause request */
#define LMP_EXT_PAUSE_ENCRYPTION_AES_REQ 0x23

/*
 * LMP PDU Structures
 */

/* LMP PDU header */
struct pdu_lmp_header {
#ifdef CONFIG_LITTLE_ENDIAN
	uint8_t tid:1;        /* Transaction ID */
	uint8_t opcode:7;     /* Opcode */
#else
	uint8_t opcode:7;
	uint8_t tid:1;
#endif
} __packed;

/* LMP Extended PDU header */
struct pdu_lmp_ext_header {
#ifdef CONFIG_LITTLE_ENDIAN
	uint8_t tid:1;        /* Transaction ID */
	uint8_t escape:7;     /* Escape code (0x7C-0x7F) */
#else
	uint8_t escape:7;
	uint8_t tid:1;
#endif
	uint8_t ext_opcode;   /* Extended opcode */
} __packed;

/* LMP_accepted */
struct pdu_lmp_accepted {
	struct pdu_lmp_header hdr;
	uint8_t opcode;       /* Opcode being accepted */
} __packed;

/* LMP_not_accepted */
struct pdu_lmp_not_accepted {
	struct pdu_lmp_header hdr;
	uint8_t opcode;       /* Opcode being rejected */
	uint8_t error_code;   /* Error code */
} __packed;

/* LMP_version_req / LMP_version_res */
struct pdu_lmp_version {
	struct pdu_lmp_header hdr;
	uint8_t  vers_nr;     /* Version number */
	uint16_t comp_id;     /* Company identifier */
	uint16_t sub_vers_nr; /* Subversion number */
} __packed;

/* LMP_features_req / LMP_features_res */
struct pdu_lmp_features {
	struct pdu_lmp_header hdr;
	uint8_t features[8];  /* Features bitmap */
} __packed;

/* LMP_name_req */
struct pdu_lmp_name_req {
	struct pdu_lmp_header hdr;
	uint8_t name_offset;  /* Name fragment offset */
} __packed;

/* LMP_name_res */
struct pdu_lmp_name_res {
	struct pdu_lmp_header hdr;
	uint8_t name_offset;  /* Name fragment offset */
	uint8_t name_length;  /* Total name length */
	uint8_t name_frag[14]; /* Name fragment */
} __packed;

/* LMP_detach */
struct pdu_lmp_detach {
	struct pdu_lmp_header hdr;
	uint8_t reason;       /* Reason for detachment */
} __packed;

/* LMP_clkoffset_req / LMP_clkoffset_res */
struct pdu_lmp_clkoffset {
	struct pdu_lmp_header hdr;
	uint16_t clock_offset; /* Clock offset */
} __packed;

/* LMP_host_connection_req */
struct pdu_lmp_host_conn_req {
	struct pdu_lmp_header hdr;
	/* No additional parameters */
} __packed;

/* LMP_setup_complete */
struct pdu_lmp_setup_complete {
	struct pdu_lmp_header hdr;
	/* No additional parameters */
} __packed;

/* LMP_quality_of_service */
struct pdu_lmp_qos {
	struct pdu_lmp_header hdr;
	uint16_t poll_interval;
	uint8_t  nbc;
} __packed;

/* LMP_quality_of_service_req */
struct pdu_lmp_qos_req {
	struct pdu_lmp_header hdr;
	uint16_t poll_interval;
	uint8_t  nbc;
} __packed;

/* LMP_sniff_req */
struct pdu_lmp_sniff_req {
	struct pdu_lmp_header hdr;
	uint8_t  timing_control_flags;
	uint16_t d_sniff;     /* Offset */
	uint16_t t_sniff;     /* Interval */
	uint16_t sniff_attempt;
	uint16_t sniff_timeout;
} __packed;

/* LMP_unsniff_req */
struct pdu_lmp_unsniff_req {
	struct pdu_lmp_header hdr;
	/* No additional parameters */
} __packed;

/* LMP_SCO_link_req */
struct pdu_lmp_sco_link_req {
	struct pdu_lmp_header hdr;
	uint8_t  sco_handle;
	uint8_t  timing_control_flags;
	uint8_t  d_sco;
	uint8_t  t_sco;
	uint8_t  sco_packet;
	uint8_t  air_mode;
} __packed;

/* LMP_remove_SCO_link_req */
struct pdu_lmp_remove_sco_link_req {
	struct pdu_lmp_header hdr;
	uint8_t sco_handle;
	uint8_t reason;
} __packed;

/* LMP_switch_req */
struct pdu_lmp_switch_req {
	struct pdu_lmp_header hdr;
	uint32_t switch_instant;
} __packed;

/* LMP_hold / LMP_hold_req */
struct pdu_lmp_hold {
	struct pdu_lmp_header hdr;
	uint16_t hold_time;
	uint32_t hold_instant;
} __packed;

/* LMP_supervision_timeout */
struct pdu_lmp_supervision_timeout {
	struct pdu_lmp_header hdr;
	uint16_t supervision_timeout;
} __packed;

/* LMP_set_AFH */
struct pdu_lmp_set_afh {
	struct pdu_lmp_header hdr;
	uint32_t afh_instant;
	uint8_t  afh_mode;
	uint8_t  afh_channel_map[10];
} __packed;

/* LMP_encryption_mode_req */
struct pdu_lmp_encryption_mode_req {
	struct pdu_lmp_header hdr;
	uint8_t encryption_mode;
} __packed;

/* LMP_encryption_key_size_req */
struct pdu_lmp_encryption_key_size_req {
	struct pdu_lmp_header hdr;
	uint8_t key_size;
} __packed;

/* LMP_start_encryption_req */
struct pdu_lmp_start_encryption_req {
	struct pdu_lmp_header hdr;
	uint8_t random_number[16];
} __packed;

/* LMP_au_rand */
struct pdu_lmp_au_rand {
	struct pdu_lmp_header hdr;
	uint8_t random_number[16];
} __packed;

/* LMP_sres */
struct pdu_lmp_sres {
	struct pdu_lmp_header hdr;
	uint8_t auth_res[4];
} __packed;

/*
 * Extended LMP PDU Structures
 */

/* LMP_ext_accepted */
struct pdu_lmp_ext_accepted {
	struct pdu_lmp_ext_header hdr;
	uint8_t escape_opcode;
	uint8_t ext_opcode;
} __packed;

/* LMP_ext_not_accepted */
struct pdu_lmp_ext_not_accepted {
	struct pdu_lmp_ext_header hdr;
	uint8_t escape_opcode;
	uint8_t ext_opcode;
	uint8_t error_code;
} __packed;

/* LMP_ext_features_req / LMP_ext_features_res */
struct pdu_lmp_ext_features {
	struct pdu_lmp_ext_header hdr;
	uint8_t features_page;
	uint8_t max_supported_page;
	uint8_t features[8];
} __packed;

/* LMP_eSCO_link_req */
struct pdu_lmp_esco_link_req {
	struct pdu_lmp_ext_header hdr;
	uint8_t  esco_handle;
	uint8_t  esco_lt_addr;
	uint8_t  timing_control_flags;
	uint8_t  d_esco;
	uint8_t  t_esco;
	uint8_t  w_esco;
	uint8_t  esco_packet_type_m_to_s;
	uint8_t  esco_packet_type_s_to_m;
	uint16_t packet_length_m_to_s;
	uint16_t packet_length_s_to_m;
	uint8_t  air_mode;
	uint8_t  negotiation_state;
} __packed;

/* LMP_remove_eSCO_link_req */
struct pdu_lmp_remove_esco_link_req {
	struct pdu_lmp_ext_header hdr;
	uint8_t esco_handle;
	uint8_t reason;
} __packed;

/* LMP_IO_capability_req / LMP_IO_capability_res */
struct pdu_lmp_io_capability {
	struct pdu_lmp_ext_header hdr;
	uint8_t io_capability;
	uint8_t oob_data_present;
	uint8_t auth_requirements;
} __packed;

/* LMP_simple_pairing_confirm */
struct pdu_lmp_sp_confirm {
	struct pdu_lmp_header hdr;
	uint8_t commitment_value[16];
} __packed;

/* LMP_simple_pairing_number */
struct pdu_lmp_sp_number {
	struct pdu_lmp_header hdr;
	uint8_t nonce[16];
} __packed;

/* LMP_DHkey_check */
struct pdu_lmp_dhkey_check {
	struct pdu_lmp_header hdr;
	uint8_t confirmation_value[16];
} __packed;

/* LMP_power_control_req */
struct pdu_lmp_power_control_req {
	struct pdu_lmp_ext_header hdr;
	uint8_t power_adjustment;
} __packed;

/* LMP_power_control_res */
struct pdu_lmp_power_control_res {
	struct pdu_lmp_ext_header hdr;
	uint8_t power_adjustment;
} __packed;

/*
 * BR/EDR Connection States
 */
enum bredr_conn_state {
	BREDR_CONN_STATE_IDLE = 0,
	BREDR_CONN_STATE_PAGE,
	BREDR_CONN_STATE_PAGE_SCAN,
	BREDR_CONN_STATE_PAGE_RESP,
	BREDR_CONN_STATE_CENTRAL_RESP,
	BREDR_CONN_STATE_CONNECTED,
	BREDR_CONN_STATE_HOLD,
	BREDR_CONN_STATE_SNIFF,
	BREDR_CONN_STATE_PARK,
};

/*
 * BR/EDR Device States
 */
enum bredr_device_state {
	BREDR_STATE_STANDBY = 0,
	BREDR_STATE_INQUIRY,
	BREDR_STATE_INQUIRY_SCAN,
	BREDR_STATE_PAGE,
	BREDR_STATE_PAGE_SCAN,
	BREDR_STATE_CENTRAL,
	BREDR_STATE_PERIPHERAL,
};

/*
 * BR/EDR Role
 */
enum bredr_role {
	BREDR_ROLE_CENTRAL = 0,
	BREDR_ROLE_PERIPHERAL = 1,
};

/*
 * BR/EDR Encryption Mode
 */
enum bredr_encryption_mode {
	BREDR_ENCRYPTION_OFF = 0,
	BREDR_ENCRYPTION_E0 = 1,       /* E0 encryption (legacy) */
	BREDR_ENCRYPTION_AES_CCM = 2,  /* AES-CCM encryption (SC) */
};

/*
 * BR/EDR Air Mode (for SCO/eSCO)
 */
enum bredr_air_mode {
	BREDR_AIR_MODE_U_LAW = 0,
	BREDR_AIR_MODE_A_LAW = 1,
	BREDR_AIR_MODE_CVSD = 2,
	BREDR_AIR_MODE_TRANSPARENT = 3,
};

/*
 * BR/EDR Link Policy Settings
 */
#define BREDR_LINK_POLICY_ROLE_SWITCH    BIT(0)
#define BREDR_LINK_POLICY_HOLD_MODE      BIT(1)
#define BREDR_LINK_POLICY_SNIFF_MODE     BIT(2)
#define BREDR_LINK_POLICY_PARK_STATE     BIT(3)

/*
 * BR/EDR Page Scan Repetition Mode
 */
enum bredr_page_scan_rep_mode {
	BREDR_PAGE_SCAN_REP_MODE_R0 = 0,
	BREDR_PAGE_SCAN_REP_MODE_R1 = 1,
	BREDR_PAGE_SCAN_REP_MODE_R2 = 2,
};

/*
 * Helper macros
 */
#define BREDR_BD_ADDR_SIZE               6
#define BREDR_CLASS_OF_DEVICE_SIZE       3
#define BREDR_LINK_KEY_SIZE              16
#define BREDR_PIN_CODE_MAX_SIZE          16

/*
 * BR/EDR HCI Event Codes (from Bluetooth Core Spec Vol 4, Part E)
 * These are needed for BR/EDR Controller implementation
 */
#ifndef BT_HCI_EVT_MAX_SLOTS_CHANGED
#define BT_HCI_EVT_MAX_SLOTS_CHANGED            0x1B
#endif

#ifndef BT_HCI_EVT_CONN_PKT_TYPE_CHANGED
#define BT_HCI_EVT_CONN_PKT_TYPE_CHANGED        0x1D
#endif

/*
 * BR/EDR HCI Event Structures
 */
struct bt_hci_evt_max_slots_changed {
	uint16_t handle;
	uint8_t  max_slots;
} __packed;

struct bt_hci_evt_conn_pkt_type_changed {
	uint8_t  status;
	uint16_t handle;
	uint16_t pkt_type;
} __packed;

/*
 * BR/EDR HCI Error Codes
 */
#ifndef BT_HCI_ERR_UNSUPP_LMP_PARAM_VAL
#define BT_HCI_ERR_UNSUPP_LMP_PARAM_VAL         0x20
#endif

#endif /* SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_PDU_BREDR_H_ */
