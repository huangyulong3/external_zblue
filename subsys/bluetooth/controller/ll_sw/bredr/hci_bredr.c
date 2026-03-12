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
 * BR/EDR HCI Implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/sys/byteorder.h>

#include "util/util.h"
#include "util/mem.h"
#include "util/memq.h"

#include "pdu_bredr.h"
#include "lll_bredr.h"
#include "ull_bredr.h"
#include "lmp_proc.h"
#include "hci_bredr.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_ctlr_hci_bredr, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

/* Local state */
static uint8_t local_bd_addr[6];
static uint8_t local_name[248];
static uint8_t local_name_len;
static uint8_t class_of_device[3];
static uint8_t scan_enable;
static uint16_t page_timeout = 0x2000;  /* 5.12 seconds */
static uint16_t conn_accept_timeout = 0x1FA0;  /* 5 seconds */
static uint16_t page_scan_interval = 0x0800;
static uint16_t page_scan_window = 0x0012;
static uint16_t inquiry_scan_interval = 0x0800;
static uint16_t inquiry_scan_window = 0x0012;
static uint8_t authentication_enable;
static uint8_t simple_pairing_mode = 1;
static uint16_t voice_setting = 0x0060;  /* CVSD, 16-bit linear */
static uint8_t eir_data[240];
static uint8_t eir_fec_required;
static uint16_t default_link_policy;
static uint8_t page_scan_type;
static uint8_t inquiry_scan_type;
static uint8_t afh_assessment_mode = 1;
static int8_t inquiry_tx_power;
static uint8_t erroneous_data_reporting;
static uint8_t pin_type;
static uint8_t num_iac = 1;
static uint8_t iac_lap[3] = {0x33, 0x8B, 0x9E};  /* GIAC */

/* Local features - BR/EDR Controller capabilities */
static const uint8_t local_features[8] = {
	0xFF,  /* 3-slot, 5-slot, encryption, slot offset, timing accuracy, role switch, hold, sniff */
	0xFF,  /* Park, RSSI, channel quality, SCO, HV2, HV3, u-law, A-law */
	0x8D,  /* CVSD, paging scheme, power control, transparent SCO, broadcast encrypt */
	0xFE,  /* EDR ACL 2M, EDR ACL 3M, enhanced inquiry, interlaced inquiry, interlaced page, RSSI inquiry, EV3, EV4 */
	0xDB,  /* EV5, AFH capable peripheral, AFH classification peripheral, BR/EDR not supported (0), LE supported (0), 3-slot EDR ACL, 5-slot EDR ACL, sniff subrating */
	0x9B,  /* Pause encryption, AFH capable central, AFH classification central, EDR eSCO 2M, EDR eSCO 3M, 3-slot EDR eSCO, extended inquiry, LE+BR/EDR same device (0) */
	0x00,  /* SSP (host), secure connections (host), ... */
	0x00,
};

/* Extended features page 1 - Host supported features */
static uint8_t ext_features_page1[8] = {
	0x03,  /* SSP host support, LE host support */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* Extended features page 2 - Controller supported features */
static const uint8_t ext_features_page2[8] = {
	0x0F,  /* CSB central, CSB peripheral, sync train, sync scan */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/*
 * Link Control Commands
 */
int hci_bredr_inquiry(uint8_t *lap, uint8_t inquiry_length, uint8_t num_responses)
{
	return ull_bredr_inquiry_start(lap, inquiry_length, num_responses);
}

int hci_bredr_inquiry_cancel(void)
{
	return ull_bredr_inquiry_stop();
}

int hci_bredr_create_connection(uint8_t *bd_addr, uint16_t packet_type,
				uint8_t page_scan_rep_mode, uint8_t reserved,
				uint16_t clock_offset, uint8_t allow_role_switch)
{
	ARG_UNUSED(packet_type);
	ARG_UNUSED(reserved);
	ARG_UNUSED(allow_role_switch);

	return ull_bredr_page_start(bd_addr, page_scan_rep_mode, clock_offset);
}

int hci_bredr_disconnect(uint16_t handle, uint8_t reason)
{
	return ull_bredr_conn_disconnect(handle, reason);
}

int hci_bredr_create_connection_cancel(uint8_t *bd_addr)
{
	ARG_UNUSED(bd_addr);
	return ull_bredr_page_stop();
}

int hci_bredr_accept_connection_request(uint8_t *bd_addr, uint8_t role)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get_by_addr(bd_addr);
	if (!conn) {
		return -ENOENT;
	}

	/* Accept the connection with specified role
	 * If role negotiation is needed, initiate role switch after connection
	 */
	if (role != conn->lll.role) {
		/* Request role switch after connection setup */
		conn->req.loc_switch_req = true;
	}

	/* Mark connection as accepted by host */
	conn->link.host_connected = true;

	return 0;
}

int hci_bredr_reject_connection_request(uint8_t *bd_addr, uint8_t reason)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get_by_addr(bd_addr);
	if (!conn) {
		return -ENOENT;
	}

	ull_bredr_conn_cleanup(conn, reason);
	return 0;
}

int hci_bredr_authentication_requested(uint16_t handle)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get(handle);
	if (!conn) {
		return -ENOENT;
	}

	/* Start authentication procedure */
	conn->auth_pending = 1;
	conn->req.loc_auth_req = true;
	conn->link.initiator = true;

	/* Initiate LMP authentication */
	return lmp_proc_auth_initiate(conn);
}

int hci_bredr_set_connection_encryption(uint16_t handle, uint8_t enable)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get(handle);
	if (!conn) {
		return -ENOENT;
	}

	conn->enc_pending = 1;

	if (enable) {
		return ull_bredr_lmp_encryption_mode_req(conn, BREDR_ENCRYPTION_E0);
	} else {
		return ull_bredr_lmp_stop_encryption_req(conn);
	}
}

int hci_bredr_remote_name_request(uint8_t *bd_addr, uint8_t page_scan_rep_mode,
				  uint8_t reserved, uint16_t clock_offset)
{
	struct ull_bredr_conn *conn;

	ARG_UNUSED(page_scan_rep_mode);
	ARG_UNUSED(reserved);
	ARG_UNUSED(clock_offset);

	conn = ull_bredr_conn_get_by_addr(bd_addr);
	if (!conn) {
		/* Need to page first */
		return -ENOTCONN;
	}

	return ull_bredr_lmp_name_req(conn, 0);
}

int hci_bredr_read_remote_supported_features(uint16_t handle)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get(handle);
	if (!conn) {
		return -ENOENT;
	}

	return ull_bredr_lmp_features_req(conn);
}

int hci_bredr_read_remote_version_information(uint16_t handle)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get(handle);
	if (!conn) {
		return -ENOENT;
	}

	return ull_bredr_lmp_version_req(conn);
}

/*
 * Link Policy Commands
 */
int hci_bredr_sniff_mode(uint16_t handle, uint16_t max_interval, uint16_t min_interval,
			 uint16_t attempt, uint16_t timeout)
{
	struct ull_bredr_conn *conn;
	uint16_t interval;

	conn = ull_bredr_conn_get(handle);
	if (!conn) {
		return -ENOENT;
	}

	/* Use average of min and max */
	interval = (max_interval + min_interval) / 2;

	return ull_bredr_lmp_sniff_req(conn, 0, interval, attempt, timeout);
}

int hci_bredr_exit_sniff_mode(uint16_t handle)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get(handle);
	if (!conn) {
		return -ENOENT;
	}

	return ull_bredr_lmp_unsniff_req(conn);
}

int hci_bredr_role_discovery(uint16_t handle, uint8_t *role)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get(handle);
	if (!conn) {
		return -ENOENT;
	}

	*role = conn->lll.role;
	return 0;
}

int hci_bredr_switch_role(uint8_t *bd_addr, uint8_t role)
{
	struct ull_bredr_conn *conn;
	uint32_t instant;

	conn = ull_bredr_conn_get_by_addr(bd_addr);
	if (!conn) {
		return -ENOENT;
	}

	if (conn->lll.role == role) {
		return 0;  /* Already in requested role */
	}

	/* Calculate switch instant - at least 2*Tpoll in the future */
	instant = conn->link.poll_interval * 2;
	if (instant < 32) {
		instant = 32;  /* Minimum 32 slots */
	}

	return ull_bredr_lmp_switch_req(conn, instant);
}

int hci_bredr_read_link_policy_settings(uint16_t handle, uint16_t *settings)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get(handle);
	if (!conn) {
		return -ENOENT;
	}

	/* Return per-connection link policy settings */
	*settings = conn->link.link_policy_settings;
	return 0;
}

int hci_bredr_write_link_policy_settings(uint16_t handle, uint16_t settings)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get(handle);
	if (!conn) {
		return -ENOENT;
	}

	/* Store per-connection link policy settings */
	conn->link.link_policy_settings = settings;
	return 0;
}

int hci_bredr_read_default_link_policy_settings(uint16_t *settings)
{
	*settings = default_link_policy;
	return 0;
}

int hci_bredr_write_default_link_policy_settings(uint16_t settings)
{
	default_link_policy = settings;
	return 0;
}

/*
 * Controller & Baseband Commands
 */
int hci_bredr_reset(void)
{
	return ull_bredr_reset();
}

int hci_bredr_write_local_name(uint8_t *name)
{
	memcpy(local_name, name, 248);
	local_name_len = strlen((char *)name);
	if (local_name_len > 248) {
		local_name_len = 248;
	}
	return 0;
}

int hci_bredr_read_local_name(uint8_t *name)
{
	memcpy(name, local_name, 248);
	return 0;
}

int hci_bredr_read_connection_accept_timeout(uint16_t *timeout)
{
	*timeout = conn_accept_timeout;
	return 0;
}

int hci_bredr_write_connection_accept_timeout(uint16_t timeout)
{
	conn_accept_timeout = timeout;
	return 0;
}

int hci_bredr_read_page_timeout(uint16_t *timeout)
{
	*timeout = page_timeout;
	return 0;
}

int hci_bredr_write_page_timeout(uint16_t timeout)
{
	page_timeout = timeout;
	return 0;
}

int hci_bredr_read_scan_enable(uint8_t *enable)
{
	*enable = scan_enable;
	return 0;
}

int hci_bredr_write_scan_enable(uint8_t enable)
{
	int err = 0;

	/* Inquiry scan */
	if ((enable & BIT(0)) != (scan_enable & BIT(0))) {
		err = ull_bredr_inquiry_scan_enable(enable & BIT(0));
		if (err) {
			return err;
		}
	}

	/* Page scan */
	if ((enable & BIT(1)) != (scan_enable & BIT(1))) {
		err = ull_bredr_page_scan_enable((enable & BIT(1)) >> 1);
		if (err) {
			return err;
		}
	}

	scan_enable = enable;
	return 0;
}

int hci_bredr_read_page_scan_activity(uint16_t *interval, uint16_t *window)
{
	*interval = page_scan_interval;
	*window = page_scan_window;
	return 0;
}

int hci_bredr_write_page_scan_activity(uint16_t interval, uint16_t window)
{
	page_scan_interval = interval;
	page_scan_window = window;
	return ull_bredr_page_scan_set_params(interval, window);
}

int hci_bredr_read_inquiry_scan_activity(uint16_t *interval, uint16_t *window)
{
	*interval = inquiry_scan_interval;
	*window = inquiry_scan_window;
	return 0;
}

int hci_bredr_write_inquiry_scan_activity(uint16_t interval, uint16_t window)
{
	inquiry_scan_interval = interval;
	inquiry_scan_window = window;
	return ull_bredr_inquiry_scan_set_params(interval, window);
}

int hci_bredr_read_authentication_enable(uint8_t *enable)
{
	*enable = authentication_enable;
	return 0;
}

int hci_bredr_write_authentication_enable(uint8_t enable)
{
	authentication_enable = enable;
	return 0;
}

int hci_bredr_read_class_of_device(uint8_t *cod)
{
	memcpy(cod, class_of_device, 3);
	return 0;
}

int hci_bredr_write_class_of_device(uint8_t *cod)
{
	memcpy(class_of_device, cod, 3);
	return 0;
}

int hci_bredr_read_voice_setting(uint16_t *setting)
{
	*setting = voice_setting;
	return 0;
}

int hci_bredr_write_voice_setting(uint16_t setting)
{
	voice_setting = setting;
	return 0;
}

int hci_bredr_read_link_supervision_timeout(uint16_t handle, uint16_t *timeout)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get(handle);
	if (!conn) {
		return -ENOENT;
	}

	*timeout = conn->supervision_timeout;
	return 0;
}

int hci_bredr_write_link_supervision_timeout(uint16_t handle, uint16_t timeout)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get(handle);
	if (!conn) {
		return -ENOENT;
	}

	conn->supervision_timeout = timeout;
	conn->lll.supervision_timeout = timeout;
	return 0;
}

int hci_bredr_read_number_of_supported_iac(uint8_t *num)
{
	*num = 4;  /* Support up to 4 IACs */
	return 0;
}

int hci_bredr_read_current_iac_lap(uint8_t *num, uint8_t *lap)
{
	*num = num_iac;
	memcpy(lap, iac_lap, 3 * num_iac);
	return 0;
}

int hci_bredr_write_current_iac_lap(uint8_t num, uint8_t *lap)
{
	if (num > 4) {
		return -EINVAL;
	}
	num_iac = num;
	memcpy(iac_lap, lap, 3 * num);
	return 0;
}

int hci_bredr_read_page_scan_type(uint8_t *type)
{
	*type = page_scan_type;
	return 0;
}

int hci_bredr_write_page_scan_type(uint8_t type)
{
	page_scan_type = type;
	return 0;
}

int hci_bredr_read_afh_channel_assessment_mode(uint8_t *mode)
{
	*mode = afh_assessment_mode;
	return 0;
}

int hci_bredr_write_afh_channel_assessment_mode(uint8_t mode)
{
	afh_assessment_mode = mode;
	return 0;
}

int hci_bredr_read_extended_inquiry_response(uint8_t *fec_required, uint8_t *data)
{
	*fec_required = eir_fec_required;
	memcpy(data, eir_data, 240);
	return 0;
}

int hci_bredr_write_extended_inquiry_response(uint8_t fec_required, uint8_t *data)
{
	eir_fec_required = fec_required;
	memcpy(eir_data, data, 240);
	return 0;
}

int hci_bredr_read_simple_pairing_mode(uint8_t *mode)
{
	*mode = simple_pairing_mode;
	return 0;
}

int hci_bredr_write_simple_pairing_mode(uint8_t mode)
{
	simple_pairing_mode = mode;
	/* Update host features */
	if (mode) {
		ext_features_page1[0] |= BIT(0);
	} else {
		ext_features_page1[0] &= ~BIT(0);
	}
	return 0;
}

int hci_bredr_read_inquiry_response_transmit_power_level(int8_t *level)
{
	*level = inquiry_tx_power;
	return 0;
}

int hci_bredr_write_inquiry_transmit_power_level(int8_t level)
{
	inquiry_tx_power = level;
	return 0;
}

int hci_bredr_read_default_erroneous_data_reporting(uint8_t *enable)
{
	*enable = erroneous_data_reporting;
	return 0;
}

int hci_bredr_write_default_erroneous_data_reporting(uint8_t enable)
{
	erroneous_data_reporting = enable;
	return 0;
}

/*
 * Informational Parameters
 */
int hci_bredr_read_local_version_information(uint8_t *hci_version, uint16_t *hci_revision,
					     uint8_t *lmp_version, uint16_t *manufacturer,
					     uint16_t *lmp_subversion)
{
	*hci_version = 0x0C;     /* Bluetooth 6.0 */
	*hci_revision = 0x0001;
	*lmp_version = 0x0C;     /* LMP 6.0 */
	*manufacturer = 0xFFFF;  /* Company ID */
	*lmp_subversion = 0x0001;
	return 0;
}

int hci_bredr_read_local_supported_features(uint8_t *features)
{
	memcpy(features, local_features, 8);
	return 0;
}

int hci_bredr_read_local_extended_features(uint8_t page_number, uint8_t *max_page,
					   uint8_t *features)
{
	*max_page = 2;

	switch (page_number) {
	case 0:
		memcpy(features, local_features, 8);
		break;
	case 1:
		memcpy(features, ext_features_page1, 8);
		break;
	case 2:
		memcpy(features, ext_features_page2, 8);
		break;
	default:
		memset(features, 0, 8);
		break;
	}

	return 0;
}

int hci_bredr_read_buffer_size(uint16_t *acl_mtu, uint8_t *sco_mtu,
			       uint16_t *acl_max_pkt, uint16_t *sco_max_pkt)
{
	*acl_mtu = 1021;  /* Max ACL payload (3-DH5) */
	*sco_mtu = 60;    /* Max SCO payload */
	*acl_max_pkt = CONFIG_BT_CTLR_BREDR_TX_BUFFERS;
	*sco_max_pkt = ULL_BREDR_SCO_MAX;
	return 0;
}

int hci_bredr_read_bd_addr(uint8_t *bd_addr)
{
	memcpy(bd_addr, local_bd_addr, 6);
	return 0;
}

/*
 * Status Parameters
 */
int hci_bredr_read_rssi(uint16_t handle, int8_t *rssi)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get(handle);
	if (!conn) {
		return -ENOENT;
	}

	*rssi = conn->lll.rssi;
	return 0;
}

int hci_bredr_read_afh_channel_map(uint16_t handle, uint8_t *mode, uint8_t *map)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get(handle);
	if (!conn) {
		return -ENOENT;
	}

	*mode = conn->lll.afh_enabled;
	memcpy(map, conn->lll.afh_channel_map, 10);
	return 0;
}

int hci_bredr_read_encryption_key_size(uint16_t handle, uint8_t *key_size)
{
	struct ull_bredr_conn *conn;

	conn = ull_bredr_conn_get(handle);
	if (!conn) {
		return -ENOENT;
	}

	*key_size = conn->lll.encryption_key_size;
	return 0;
}

/*
 * Testing Commands
 */
int hci_bredr_write_simple_pairing_debug_mode(uint8_t mode)
{
	/* Store SSP debug mode setting
	 * When enabled, uses fixed debug keys for SSP
	 */
	struct ull_bredr_lm_env *lm_env = ull_bredr_lm_env_get();

	lm_env->hci.sp_debug_mode = mode;

	LOG_DBG("SSP debug mode: %s", mode ? "enabled" : "disabled");
	return 0;
}
