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

/**
 * @brief z_api implementation for Zblue environment
 *
 * This file provides implementations of z_api() functions that simply
 * call the original Zephyr Bluetooth APIs. This allows the tester code
 * to use z_api() macros while still calling the real Zephyr functions.
 *
 * This file is only compiled when CONFIG_Z_API_USE_FRAMEWORK is NOT defined.
 * When CONFIG_Z_API_USE_FRAMEWORK is defined, the z_api functions are
 * provided by the z_port layer (frameworks/connectivity/bluetooth/z_port).
 */

#ifdef CONFIG_Z_API_USE_FRAMEWORK

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/l2cap.h>
#include <errno.h>

/* GAP APIs */
int z_bt_enable(bt_ready_cb_t cb) { return bt_enable(cb); }
int z_bt_disable(void) { return bt_disable(); }

int z_bt_le_adv_start(const struct bt_le_adv_param *param,
                      const struct bt_data *ad, size_t ad_len,
                      const struct bt_data *sd, size_t sd_len)
{
    return bt_le_adv_start(param, ad, ad_len, sd, sd_len);
}
int z_bt_le_adv_stop(void) { return bt_le_adv_stop(); }

int z_bt_le_scan_start(const struct bt_le_scan_param *param, bt_le_scan_cb_t cb)
{
    return bt_le_scan_start(param, cb);
}
int z_bt_le_scan_stop(void) { return bt_le_scan_stop(); }

/* Connection APIs */
int z_bt_conn_le_create(const bt_addr_le_t *peer,
                        const struct bt_conn_le_create_param *create_param,
                        const struct bt_le_conn_param *conn_param,
                        struct bt_conn **ret_conn)
{
    return bt_conn_le_create(peer, create_param, conn_param, ret_conn);
}

int z_bt_conn_le_create_auto(const struct bt_conn_le_create_param *create_param,
                             const struct bt_le_conn_param *conn_param)
{
    return bt_conn_le_create_auto(create_param, conn_param);
}

int z_bt_conn_disconnect(struct bt_conn *conn, uint8_t reason)
{
    return bt_conn_disconnect(conn, reason);
}

struct bt_conn *z_bt_conn_lookup_addr_le(uint8_t id, const bt_addr_le_t *peer)
{
    return bt_conn_lookup_addr_le(id, peer);
}

void z_bt_conn_unref(struct bt_conn *conn) { bt_conn_unref(conn); }
struct bt_conn *z_bt_conn_ref(struct bt_conn *conn) { return bt_conn_ref(conn); }

int z_bt_conn_get_info(struct bt_conn *conn, struct bt_conn_info *info)
{
    return bt_conn_get_info(conn, info);
}

const bt_addr_le_t *z_bt_conn_get_dst(const struct bt_conn *conn)
{
    return bt_conn_get_dst(conn);
}

bt_security_t z_bt_conn_get_security(struct bt_conn *conn)
{
    return bt_conn_get_security(conn);
}

void z_bt_conn_cb_register(struct bt_conn_cb *cb) { bt_conn_cb_register(cb); }
void z_bt_conn_cb_unregister(struct bt_conn_cb *cb) { bt_conn_cb_unregister(cb); }

int z_bt_conn_le_param_update(struct bt_conn *conn, const struct bt_le_conn_param *param)
{
    return bt_conn_le_param_update(conn, param);
}

int z_bt_conn_set_security(struct bt_conn *conn, bt_security_t sec)
{
    return bt_conn_set_security(conn, sec);
}

uint8_t z_bt_conn_enc_key_size(struct bt_conn *conn)
{
    return bt_conn_enc_key_size(conn);
}

/* Authentication APIs */
int z_bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb)
{
    return bt_conn_auth_cb_register(cb);
}

int z_bt_conn_auth_info_cb_register(struct bt_conn_auth_info_cb *cb)
{
    return bt_conn_auth_info_cb_register(cb);
}

int z_bt_conn_auth_passkey_entry(struct bt_conn *conn, unsigned int passkey)
{
    return bt_conn_auth_passkey_entry(conn, passkey);
}

int z_bt_conn_auth_passkey_confirm(struct bt_conn *conn)
{
    return bt_conn_auth_passkey_confirm(conn);
}

int z_bt_conn_auth_cancel(struct bt_conn *conn)
{
    return bt_conn_auth_cancel(conn);
}

/* Bonding APIs */
int z_bt_set_bondable(bool enable) { bt_set_bondable(enable); return 0; }

int z_bt_unpair(uint8_t id, const bt_addr_le_t *addr)
{
    return bt_unpair(id, addr);
}

bool z_bt_addr_le_is_bonded(uint8_t id, const bt_addr_le_t *addr)
{
    return bt_addr_le_is_bonded(id, addr);
}

/* OOB APIs */
int z_bt_le_oob_get_local(uint8_t id, struct bt_le_oob *oob)
{
    return bt_le_oob_get_local(id, oob);
}

int z_bt_le_oob_set_sc_data(struct bt_conn *conn,
                            const struct bt_le_oob_sc_data *oobd_local,
                            const struct bt_le_oob_sc_data *oobd_remote)
{
    return bt_le_oob_set_sc_data(conn, oobd_local, oobd_remote);
}

int z_bt_le_oob_set_legacy_tk(struct bt_conn *conn, const uint8_t *tk)
{
    return bt_le_oob_set_legacy_tk(conn, tk);
}

/* Filter accept list APIs */
int z_bt_le_filter_accept_list_clear(void)
{
    return bt_le_filter_accept_list_clear();
}

int z_bt_le_filter_accept_list_add(const bt_addr_le_t *addr)
{
    return bt_le_filter_accept_list_add(addr);
}

/* Extended Advertising APIs */
int z_bt_le_ext_adv_create(const struct bt_le_adv_param *param,
                           const struct bt_le_ext_adv_cb *cb,
                           struct bt_le_ext_adv **out_adv)
{
    return bt_le_ext_adv_create(param, cb, out_adv);
}

int z_bt_le_ext_adv_delete(struct bt_le_ext_adv *adv)
{
    return bt_le_ext_adv_delete(adv);
}

int z_bt_le_ext_adv_start(struct bt_le_ext_adv *adv,
                          const struct bt_le_ext_adv_start_param *param)
{
    return bt_le_ext_adv_start(adv, param);
}

int z_bt_le_ext_adv_stop(struct bt_le_ext_adv *adv)
{
    return bt_le_ext_adv_stop(adv);
}

int z_bt_le_ext_adv_set_data(struct bt_le_ext_adv *adv,
                             const struct bt_data *ad, size_t ad_len,
                             const struct bt_data *sd, size_t sd_len)
{
    return bt_le_ext_adv_set_data(adv, ad, ad_len, sd, sd_len);
}

/* GATT Server APIs */
int z_bt_gatt_service_register(struct bt_gatt_service *svc)
{
    return bt_gatt_service_register(svc);
}

int z_bt_gatt_service_unregister(struct bt_gatt_service *svc)
{
    return bt_gatt_service_unregister(svc);
}

/* GATT Notification/Indication APIs */
int z_bt_gatt_notify(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                     const void *data, uint16_t len)
{
    return bt_gatt_notify(conn, attr, data, len);
}

int z_bt_gatt_indicate(struct bt_conn *conn, struct bt_gatt_indicate_params *params)
{
    return bt_gatt_indicate(conn, params);
}

int z_bt_gatt_notify_multiple(struct bt_conn *conn, uint16_t num_params,
                              struct bt_gatt_notify_params params[])
{
    return bt_gatt_notify_multiple(conn, num_params, params);
}

/* GATT Client APIs */
int z_bt_gatt_discover(struct bt_conn *conn, struct bt_gatt_discover_params *params)
{
    return bt_gatt_discover(conn, params);
}

int z_bt_gatt_read(struct bt_conn *conn, struct bt_gatt_read_params *params)
{
    return bt_gatt_read(conn, params);
}

int z_bt_gatt_write(struct bt_conn *conn, struct bt_gatt_write_params *params)
{
    return bt_gatt_write(conn, params);
}

int z_bt_gatt_write_without_response(struct bt_conn *conn, uint16_t handle,
                                     const void *data, uint16_t length, bool sign)
{
    return bt_gatt_write_without_response(conn, handle, data, length, sign);
}

int z_bt_gatt_subscribe(struct bt_conn *conn, struct bt_gatt_subscribe_params *params)
{
    return bt_gatt_subscribe(conn, params);
}

int z_bt_gatt_unsubscribe(struct bt_conn *conn, struct bt_gatt_subscribe_params *params)
{
    return bt_gatt_unsubscribe(conn, params);
}

int z_bt_gatt_exchange_mtu(struct bt_conn *conn, struct bt_gatt_exchange_params *params)
{
    return bt_gatt_exchange_mtu(conn, params);
}

/* GATT Attribute Read Helper */
ssize_t z_bt_gatt_attr_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            void *buf, uint16_t buf_len, uint16_t offset,
                            const void *value, uint16_t value_len)
{
    return bt_gatt_attr_read(conn, attr, buf, buf_len, offset, value, value_len);
}

/* GATT Foreach Attribute */
void z_bt_gatt_foreach_attr(uint16_t start_handle, uint16_t end_handle,
                            bt_gatt_attr_func_t func, void *user_data)
{
    bt_gatt_foreach_attr(start_handle, end_handle, func, user_data);
}

/* L2CAP Server APIs */
int z_bt_l2cap_server_register(struct bt_l2cap_server *server)
{
    return bt_l2cap_server_register(server);
}

/* L2CAP Channel APIs */
int z_bt_l2cap_chan_connect(struct bt_conn *conn, struct bt_l2cap_chan *chan,
                            uint16_t psm)
{
    return bt_l2cap_chan_connect(conn, chan, psm);
}

int z_bt_l2cap_chan_disconnect(struct bt_l2cap_chan *chan)
{
    return bt_l2cap_chan_disconnect(chan);
}

int z_bt_l2cap_chan_send(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
    return bt_l2cap_chan_send(chan, buf);
}

int z_bt_l2cap_chan_recv_complete(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
    return bt_l2cap_chan_recv_complete(chan, buf);
}

/* L2CAP ECRED APIs */
int z_bt_l2cap_ecred_chan_connect(struct bt_conn *conn, struct bt_l2cap_chan **chans,
                                  uint16_t psm)
{
    return bt_l2cap_ecred_chan_connect(conn, chans, psm);
}

int z_bt_l2cap_ecred_chan_reconfigure(struct bt_l2cap_chan **chans, uint16_t mtu)
{
    return bt_l2cap_ecred_chan_reconfigure(chans, mtu);
}

/* EATT APIs */
#if defined(CONFIG_BT_EATT)
int z_bt_eatt_connect(struct bt_conn *conn, size_t num_channels)
{
    return bt_eatt_connect(conn, num_channels);
}

int z_bt_eatt_disconnect_one(struct bt_conn *conn)
{
    return bt_eatt_disconnect_one(conn);
}
#else
int z_bt_eatt_connect(struct bt_conn *conn, size_t num_channels)
{
    return -ENOTSUP;
}

int z_bt_eatt_disconnect_one(struct bt_conn *conn)
{
    return -ENOTSUP;
}
#endif

#endif /* !CONFIG_Z_API_USE_FRAMEWORK */
