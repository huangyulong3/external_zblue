/***********************************************************************
 *
 * Copyright 2026 XiaoMi All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ***********************************************************************/

#ifndef __Z_API_PORT_H__
#define __Z_API_PORT_H__

/**
 * @brief z_api port header for autopts tester
 *
 * This header provides extern declarations for all z_api() functions
 * used by the autopts tester. Include this file instead of the
 * individual z_port headers (z_api_gap.h, z_api_gatt.h, etc.).
 *
 * The z_api(name) macro expands to z_##name, e.g.:
 *   z_api(bt_enable) -> z_bt_enable
 *
 * Environment Selection:
 *   CONFIG_Z_API_USE_FRAMEWORK=y : Use Framework Bluetooth API (z_port)
 *   CONFIG_Z_API_USE_FRAMEWORK=n : Use Zephyr Bluetooth API (z_api_impl.c)
 */

/* z_api macro: z_api(bt_enable) -> z_bt_enable */
#ifndef z_api
#define z_api(name) z_##name
#endif

#ifdef STRINGIFY
#undef STRINGIFY
#endif
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/l2cap.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * When CONFIG_Z_API_USE_FRAMEWORK is enabled, the z_api functions are
 * implemented in z_port layer (frameworks/connectivity/bluetooth/z_port).
 *
 * When CONFIG_Z_API_USE_FRAMEWORK is disabled (default for Zblue),
 * the z_api functions are implemented in z_api_impl.c which directly
 * calls the original Zephyr Bluetooth APIs.
 */
#ifdef CONFIG_Z_API_USE_FRAMEWORK
/* z_api implementations come from z_port layer */
#else
/* z_api implementations come from z_api_impl.c (Zblue environment) */
#endif

/* ============================================================
 * GAP APIs
 * ============================================================ */

/* Bluetooth enable/disable */
extern int z_api(bt_enable)(bt_ready_cb_t cb);
extern int z_api(bt_disable)(void);

/* Advertising APIs */
extern int z_api(bt_le_adv_start)(const struct bt_le_adv_param *param,
                                  const struct bt_data *ad, size_t ad_len,
                                  const struct bt_data *sd, size_t sd_len);
extern int z_api(bt_le_adv_stop)(void);

/* Scanning APIs */
extern int z_api(bt_le_scan_start)(const struct bt_le_scan_param *param,
                                   bt_le_scan_cb_t cb);
extern int z_api(bt_le_scan_stop)(void);

/* Connection APIs */
extern int z_api(bt_conn_le_create)(const bt_addr_le_t *peer,
                                    const struct bt_conn_le_create_param *create_param,
                                    const struct bt_le_conn_param *conn_param,
                                    struct bt_conn **ret_conn);
extern int z_api(bt_conn_le_create_auto)(const struct bt_conn_le_create_param *create_param,
                                         const struct bt_le_conn_param *conn_param);
extern int z_api(bt_conn_disconnect)(struct bt_conn *conn, uint8_t reason);
extern struct bt_conn *z_api(bt_conn_lookup_addr_le)(uint8_t id,
                                                     const bt_addr_le_t *peer);
extern void z_api(bt_conn_unref)(struct bt_conn *conn);
extern struct bt_conn *z_api(bt_conn_ref)(struct bt_conn *conn);

/* Connection info APIs */
extern int z_api(bt_conn_get_info)(struct bt_conn *conn, struct bt_conn_info *info);
extern const bt_addr_le_t *z_api(bt_conn_get_dst)(const struct bt_conn *conn);
extern bt_security_t z_api(bt_conn_get_security)(struct bt_conn *conn);

/* Connection callback APIs */
extern void z_api(bt_conn_cb_register)(struct bt_conn_cb *cb);
extern void z_api(bt_conn_cb_unregister)(struct bt_conn_cb *cb);

/* Connection parameter update */
extern int z_api(bt_conn_le_param_update)(struct bt_conn *conn,
                                          const struct bt_le_conn_param *param);

/* Security APIs */
extern int z_api(bt_conn_set_security)(struct bt_conn *conn, bt_security_t sec);
extern uint8_t z_api(bt_conn_enc_key_size)(struct bt_conn *conn);

/* Authentication callback APIs */
extern int z_api(bt_conn_auth_cb_register)(const struct bt_conn_auth_cb *cb);
extern int z_api(bt_conn_auth_info_cb_register)(struct bt_conn_auth_info_cb *cb);
extern int z_api(bt_conn_auth_passkey_entry)(struct bt_conn *conn,
                                             unsigned int passkey);
extern int z_api(bt_conn_auth_passkey_confirm)(struct bt_conn *conn);
extern int z_api(bt_conn_auth_cancel)(struct bt_conn *conn);

/* Bonding APIs */
extern int z_api(bt_set_bondable)(bool enable);
extern int z_api(bt_unpair)(uint8_t id, const bt_addr_le_t *addr);
extern bool z_api(bt_addr_le_is_bonded)(uint8_t id, const bt_addr_le_t *addr);

/* OOB APIs */
extern int z_api(bt_le_oob_get_local)(uint8_t id, struct bt_le_oob *oob);
extern int z_api(bt_le_oob_set_sc_data)(struct bt_conn *conn,
                                        const struct bt_le_oob_sc_data *oobd_local,
                                        const struct bt_le_oob_sc_data *oobd_remote);
extern int z_api(bt_le_oob_set_legacy_tk)(struct bt_conn *conn, const uint8_t *tk);
extern int z_api(bt_le_oob_set_sc_flag)(bool enable);
extern int z_api(bt_le_oob_set_legacy_flag)(bool enable);

/* Filter accept list APIs */
extern int z_api(bt_le_filter_accept_list_clear)(void);
extern int z_api(bt_le_filter_accept_list_add)(const bt_addr_le_t *addr);

/* ============================================================
 * Extended Advertising APIs
 * ============================================================ */

extern int z_api(bt_le_ext_adv_create)(const struct bt_le_adv_param *param,
                                       const struct bt_le_ext_adv_cb *cb,
                                       struct bt_le_ext_adv **out_adv);
extern int z_api(bt_le_ext_adv_delete)(struct bt_le_ext_adv *adv);
extern int z_api(bt_le_ext_adv_start)(struct bt_le_ext_adv *adv,
                                      const struct bt_le_ext_adv_start_param *param);
extern int z_api(bt_le_ext_adv_stop)(struct bt_le_ext_adv *adv);
extern int z_api(bt_le_ext_adv_set_data)(struct bt_le_ext_adv *adv,
                                         const struct bt_data *ad, size_t ad_len,
                                         const struct bt_data *sd, size_t sd_len);

/* ============================================================
 * GATT APIs
 * ============================================================ */

/* GATT Server APIs */
extern int z_api(bt_gatt_service_register)(struct bt_gatt_service *svc);
extern int z_api(bt_gatt_service_unregister)(struct bt_gatt_service *svc);

/* GATT Notification/Indication APIs */
extern int z_api(bt_gatt_notify)(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *data, uint16_t len);
extern int z_api(bt_gatt_indicate)(struct bt_conn *conn,
                                   struct bt_gatt_indicate_params *params);
extern int z_api(bt_gatt_notify_multiple)(struct bt_conn *conn,
                                          uint16_t num_params,
                                          struct bt_gatt_notify_params params[]);

/* GATT Client APIs */
extern int z_api(bt_gatt_discover)(struct bt_conn *conn,
                                   struct bt_gatt_discover_params *params);
extern int z_api(bt_gatt_read)(struct bt_conn *conn,
                               struct bt_gatt_read_params *params);
extern int z_api(bt_gatt_write)(struct bt_conn *conn,
                                struct bt_gatt_write_params *params);
extern int z_api(bt_gatt_write_without_response)(struct bt_conn *conn,
                                                 uint16_t handle,
                                                 const void *data,
                                                 uint16_t length, bool sign);
extern int z_api(bt_gatt_subscribe)(struct bt_conn *conn,
                                    struct bt_gatt_subscribe_params *params);
extern int z_api(bt_gatt_unsubscribe)(struct bt_conn *conn,
                                      struct bt_gatt_subscribe_params *params);

/* GATT MTU Exchange */
extern int z_api(bt_gatt_exchange_mtu)(struct bt_conn *conn,
                                       struct bt_gatt_exchange_params *params);

/* GATT Attribute Read Helper */
extern ssize_t z_api(bt_gatt_attr_read)(struct bt_conn *conn,
                                        const struct bt_gatt_attr *attr,
                                        void *buf, uint16_t buf_len,
                                        uint16_t offset,
                                        const void *value,
                                        uint16_t value_len);

/* GATT Foreach Attribute */
extern void z_api(bt_gatt_foreach_attr)(uint16_t start_handle,
                                        uint16_t end_handle,
                                        bt_gatt_attr_func_t func,
                                        void *user_data);

/* ============================================================
 * L2CAP APIs
 * ============================================================ */

/* L2CAP Server APIs */
extern int z_api(bt_l2cap_server_register)(struct bt_l2cap_server *server);

/* L2CAP Channel APIs */
extern int z_api(bt_l2cap_chan_connect)(struct bt_conn *conn,
                                       struct bt_l2cap_chan *chan,
                                       uint16_t psm);
extern int z_api(bt_l2cap_chan_disconnect)(struct bt_l2cap_chan *chan);
extern int z_api(bt_l2cap_chan_send)(struct bt_l2cap_chan *chan,
                                    struct net_buf *buf);
extern int z_api(bt_l2cap_chan_recv_complete)(struct bt_l2cap_chan *chan,
                                             struct net_buf *buf);

/* L2CAP ECRED APIs */
extern int z_api(bt_l2cap_ecred_chan_connect)(struct bt_conn *conn,
                                             struct bt_l2cap_chan **chans,
                                             uint16_t psm);
extern int z_api(bt_l2cap_ecred_chan_reconfigure)(struct bt_l2cap_chan **chans,
                                                  uint16_t mtu);

/* EATT APIs */
extern int z_api(bt_eatt_connect)(struct bt_conn *conn, size_t num_channels);
extern int z_api(bt_eatt_disconnect_one)(struct bt_conn *conn);

/* ============================================================
 * BR/EDR GAP APIs
 * ============================================================ */

/* BR/EDR connection create */
extern int z_bt_conn_create(const void *peer, void **ret_conn);

/* BR/EDR discovery */
extern int z_bt_br_discovery_start(uint32_t timeout);
extern int z_bt_br_discovery_stop(void);

/* ============================================================
 * SPP / RFCOMM APIs
 * ============================================================ */

extern int z_bt_spp_connect(const uint8_t *addr, uint8_t channel);
extern int z_bt_spp_disconnect(const uint8_t *addr);
extern int z_bt_spp_listen(uint8_t channel);
extern int z_bt_spp_send(const uint8_t *addr, const uint8_t *data, uint16_t len);
extern int z_bt_rfcomm_connect(const uint8_t *addr, uint8_t channel);
extern int z_bt_rfcomm_send(const uint8_t *addr, const uint8_t *data, uint16_t len);

/* ============================================================
 * A2DP APIs
 * ============================================================ */

extern int z_bt_a2dp_connect(const uint8_t *addr);
extern int z_bt_a2dp_disconnect(const uint8_t *addr);
extern int z_bt_a2dp_start(const uint8_t *addr);
extern int z_bt_a2dp_stop(const uint8_t *addr);

/* ============================================================
 * AVRCP APIs
 * ============================================================ */

extern int z_bt_avrcp_connect(const uint8_t *addr);
extern int z_bt_avrcp_disconnect(const uint8_t *addr);
extern int z_bt_avrcp_passthrough(const uint8_t *addr,
                                  uint8_t key_id, uint8_t key_state);
extern int z_bt_avrcp_get_element_attrs(const uint8_t *addr);

/* ============================================================
 * HFP APIs
 * ============================================================ */

extern int z_bt_hfp_connect(const uint8_t *addr);
extern int z_bt_hfp_disconnect(const uint8_t *addr);
extern int z_bt_hfp_answer(const uint8_t *addr);
extern int z_bt_hfp_reject(const uint8_t *addr);
extern int z_bt_hfp_dial(const uint8_t *addr, const char *number);
extern int z_bt_hfp_set_volume(const uint8_t *addr,
                               uint8_t type, uint8_t volume);
extern int z_bt_hfp_send_dtmf(const uint8_t *addr, uint8_t code);

/* ============================================================
 * PAN APIs
 * ============================================================ */

extern int z_bt_pan_connect(const uint8_t *addr);
extern int z_bt_pan_disconnect(const uint8_t *addr);
extern int z_bt_pan_set_role(uint8_t role);

/* ============================================================
 * HID APIs
 * ============================================================ */

extern int z_bt_hid_register(uint8_t sub_class);
extern int z_bt_hid_connect(const uint8_t *addr);
extern int z_bt_hid_disconnect(const uint8_t *addr);
extern int z_bt_hid_send_report(const uint8_t *addr, uint8_t report_id,
                                const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __Z_API_PORT_H__ */
