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
 * BR/EDR HCI Interface
 * Bluetooth Core Spec Vol 4, Part E: HCI Commands for BR/EDR
 */

#ifndef SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_HCI_BREDR_H_
#define SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_HCI_BREDR_H_

#include <zephyr/types.h>
#include <zephyr/bluetooth/hci_types.h>

/*
 * BR/EDR HCI Command Handlers
 */

/* Link Control Commands (OGF 0x01) */
int hci_bredr_inquiry(uint8_t *lap, uint8_t inquiry_length, uint8_t num_responses);
int hci_bredr_inquiry_cancel(void);
int hci_bredr_periodic_inquiry_mode(uint16_t max_period, uint16_t min_period,
				    uint8_t *lap, uint8_t inquiry_length,
				    uint8_t num_responses);
int hci_bredr_exit_periodic_inquiry_mode(void);
int hci_bredr_create_connection(uint8_t *bd_addr, uint16_t packet_type,
				uint8_t page_scan_rep_mode, uint8_t reserved,
				uint16_t clock_offset, uint8_t allow_role_switch);
int hci_bredr_disconnect(uint16_t handle, uint8_t reason);
int hci_bredr_create_connection_cancel(uint8_t *bd_addr);
int hci_bredr_accept_connection_request(uint8_t *bd_addr, uint8_t role);
int hci_bredr_reject_connection_request(uint8_t *bd_addr, uint8_t reason);
int hci_bredr_link_key_request_reply(uint8_t *bd_addr, uint8_t *link_key);
int hci_bredr_link_key_request_negative_reply(uint8_t *bd_addr);
int hci_bredr_pin_code_request_reply(uint8_t *bd_addr, uint8_t pin_length,
				     uint8_t *pin_code);
int hci_bredr_pin_code_request_negative_reply(uint8_t *bd_addr);
int hci_bredr_authentication_requested(uint16_t handle);
int hci_bredr_set_connection_encryption(uint16_t handle, uint8_t enable);
int hci_bredr_remote_name_request(uint8_t *bd_addr, uint8_t page_scan_rep_mode,
				  uint8_t reserved, uint16_t clock_offset);
int hci_bredr_remote_name_request_cancel(uint8_t *bd_addr);
int hci_bredr_read_remote_supported_features(uint16_t handle);
int hci_bredr_read_remote_extended_features(uint16_t handle, uint8_t page_number);
int hci_bredr_read_remote_version_information(uint16_t handle);
int hci_bredr_read_clock_offset(uint16_t handle);
int hci_bredr_read_lmp_handle(uint16_t handle);

/* Link Policy Commands (OGF 0x02) */
int hci_bredr_hold_mode(uint16_t handle, uint16_t max_interval, uint16_t min_interval);
int hci_bredr_sniff_mode(uint16_t handle, uint16_t max_interval, uint16_t min_interval,
			 uint16_t attempt, uint16_t timeout);
int hci_bredr_exit_sniff_mode(uint16_t handle);
int hci_bredr_park_state(uint16_t handle, uint16_t max_interval, uint16_t min_interval);
int hci_bredr_exit_park_state(uint16_t handle);
int hci_bredr_qos_setup(uint16_t handle, uint8_t flags, uint8_t service_type,
			uint32_t token_rate, uint32_t peak_bandwidth,
			uint32_t latency, uint32_t delay_variation);
int hci_bredr_role_discovery(uint16_t handle, uint8_t *role);
int hci_bredr_switch_role(uint8_t *bd_addr, uint8_t role);
int hci_bredr_read_link_policy_settings(uint16_t handle, uint16_t *settings);
int hci_bredr_write_link_policy_settings(uint16_t handle, uint16_t settings);
int hci_bredr_read_default_link_policy_settings(uint16_t *settings);
int hci_bredr_write_default_link_policy_settings(uint16_t settings);
int hci_bredr_flow_specification(uint16_t handle, uint8_t flags, uint8_t flow_direction,
				 uint8_t service_type, uint32_t token_rate,
				 uint32_t token_bucket_size, uint32_t peak_bandwidth,
				 uint32_t access_latency);
int hci_bredr_sniff_subrating(uint16_t handle, uint16_t max_latency,
			      uint16_t min_remote_timeout, uint16_t min_local_timeout);

/* Controller & Baseband Commands (OGF 0x03) */
int hci_bredr_set_event_mask(uint8_t *event_mask);
int hci_bredr_reset(void);
int hci_bredr_set_event_filter(uint8_t filter_type, uint8_t filter_condition_type,
			       uint8_t *condition);
int hci_bredr_flush(uint16_t handle);
int hci_bredr_read_pin_type(uint8_t *pin_type);
int hci_bredr_write_pin_type(uint8_t pin_type);
int hci_bredr_read_stored_link_key(uint8_t *bd_addr, uint8_t read_all,
				   uint16_t *max_num_keys, uint16_t *num_keys_read);
int hci_bredr_write_stored_link_key(uint8_t num_keys, uint8_t *bd_addr_key_pairs,
				    uint8_t *num_keys_written);
int hci_bredr_delete_stored_link_key(uint8_t *bd_addr, uint8_t delete_all,
				     uint16_t *num_keys_deleted);
int hci_bredr_write_local_name(uint8_t *name);
int hci_bredr_read_local_name(uint8_t *name);
int hci_bredr_read_connection_accept_timeout(uint16_t *timeout);
int hci_bredr_write_connection_accept_timeout(uint16_t timeout);
int hci_bredr_read_page_timeout(uint16_t *timeout);
int hci_bredr_write_page_timeout(uint16_t timeout);
int hci_bredr_read_scan_enable(uint8_t *scan_enable);
int hci_bredr_write_scan_enable(uint8_t scan_enable);
int hci_bredr_read_page_scan_activity(uint16_t *interval, uint16_t *window);
int hci_bredr_write_page_scan_activity(uint16_t interval, uint16_t window);
int hci_bredr_read_inquiry_scan_activity(uint16_t *interval, uint16_t *window);
int hci_bredr_write_inquiry_scan_activity(uint16_t interval, uint16_t window);
int hci_bredr_read_authentication_enable(uint8_t *enable);
int hci_bredr_write_authentication_enable(uint8_t enable);
int hci_bredr_read_class_of_device(uint8_t *class_of_device);
int hci_bredr_write_class_of_device(uint8_t *class_of_device);
int hci_bredr_read_voice_setting(uint16_t *voice_setting);
int hci_bredr_write_voice_setting(uint16_t voice_setting);
int hci_bredr_read_automatic_flush_timeout(uint16_t handle, uint16_t *timeout);
int hci_bredr_write_automatic_flush_timeout(uint16_t handle, uint16_t timeout);
int hci_bredr_read_num_broadcast_retransmissions(uint8_t *num);
int hci_bredr_write_num_broadcast_retransmissions(uint8_t num);
int hci_bredr_read_hold_mode_activity(uint8_t *activity);
int hci_bredr_write_hold_mode_activity(uint8_t activity);
int hci_bredr_read_transmit_power_level(uint16_t handle, uint8_t type, int8_t *level);
int hci_bredr_read_synchronous_flow_control_enable(uint8_t *enable);
int hci_bredr_write_synchronous_flow_control_enable(uint8_t enable);
int hci_bredr_set_controller_to_host_flow_control(uint8_t enable);
int hci_bredr_host_buffer_size(uint16_t acl_mtu, uint8_t sco_mtu,
			       uint16_t acl_max_pkt, uint16_t sco_max_pkt);
int hci_bredr_host_number_of_completed_packets(uint8_t num_handles,
					       uint16_t *handles, uint16_t *num_packets);
int hci_bredr_read_link_supervision_timeout(uint16_t handle, uint16_t *timeout);
int hci_bredr_write_link_supervision_timeout(uint16_t handle, uint16_t timeout);
int hci_bredr_read_number_of_supported_iac(uint8_t *num_iac);
int hci_bredr_read_current_iac_lap(uint8_t *num_iac, uint8_t *iac_lap);
int hci_bredr_write_current_iac_lap(uint8_t num_iac, uint8_t *iac_lap);
int hci_bredr_read_page_scan_type(uint8_t *type);
int hci_bredr_write_page_scan_type(uint8_t type);
int hci_bredr_read_afh_channel_assessment_mode(uint8_t *mode);
int hci_bredr_write_afh_channel_assessment_mode(uint8_t mode);
int hci_bredr_read_extended_inquiry_response(uint8_t *fec_required, uint8_t *eir_data);
int hci_bredr_write_extended_inquiry_response(uint8_t fec_required, uint8_t *eir_data);
int hci_bredr_refresh_encryption_key(uint16_t handle);
int hci_bredr_read_simple_pairing_mode(uint8_t *mode);
int hci_bredr_write_simple_pairing_mode(uint8_t mode);
int hci_bredr_read_local_oob_data(uint8_t *c, uint8_t *r);
int hci_bredr_read_inquiry_response_transmit_power_level(int8_t *level);
int hci_bredr_write_inquiry_transmit_power_level(int8_t level);
int hci_bredr_read_default_erroneous_data_reporting(uint8_t *enable);
int hci_bredr_write_default_erroneous_data_reporting(uint8_t enable);
int hci_bredr_enhanced_flush(uint16_t handle, uint8_t packet_type);
int hci_bredr_send_keypress_notification(uint8_t *bd_addr, uint8_t notification_type);

/* Informational Parameters (OGF 0x04) */
int hci_bredr_read_local_version_information(uint8_t *hci_version, uint16_t *hci_revision,
					     uint8_t *lmp_version, uint16_t *manufacturer,
					     uint16_t *lmp_subversion);
int hci_bredr_read_local_supported_commands(uint8_t *commands);
int hci_bredr_read_local_supported_features(uint8_t *features);
int hci_bredr_read_local_extended_features(uint8_t page_number, uint8_t *max_page,
					   uint8_t *features);
int hci_bredr_read_buffer_size(uint16_t *acl_mtu, uint8_t *sco_mtu,
			       uint16_t *acl_max_pkt, uint16_t *sco_max_pkt);
int hci_bredr_read_bd_addr(uint8_t *bd_addr);
int hci_bredr_read_data_block_size(uint16_t *max_acl_len, uint16_t *data_block_len,
				   uint16_t *num_blocks);
int hci_bredr_read_local_supported_codecs(uint8_t *num_codecs, uint8_t *codecs);

/* Status Parameters (OGF 0x05) */
int hci_bredr_read_failed_contact_counter(uint16_t handle, uint16_t *counter);
int hci_bredr_reset_failed_contact_counter(uint16_t handle);
int hci_bredr_read_link_quality(uint16_t handle, uint8_t *quality);
int hci_bredr_read_rssi(uint16_t handle, int8_t *rssi);
int hci_bredr_read_afh_channel_map(uint16_t handle, uint8_t *mode, uint8_t *map);
int hci_bredr_read_clock(uint16_t handle, uint8_t which_clock,
			 uint32_t *clock, uint16_t *accuracy);
int hci_bredr_read_encryption_key_size(uint16_t handle, uint8_t *key_size);

/* Testing Commands (OGF 0x06) */
int hci_bredr_read_loopback_mode(uint8_t *mode);
int hci_bredr_write_loopback_mode(uint8_t mode);
int hci_bredr_enable_device_under_test_mode(void);
int hci_bredr_write_simple_pairing_debug_mode(uint8_t mode);

/*
 * BR/EDR HCI Event Generation
 */
void hci_bredr_evt_inquiry_complete(uint8_t status);
void hci_bredr_evt_inquiry_result(uint8_t num_responses, uint8_t *bd_addr,
				  uint8_t *page_scan_rep_mode, uint8_t *reserved,
				  uint8_t *class_of_device, uint16_t *clock_offset);
void hci_bredr_evt_inquiry_result_with_rssi(uint8_t num_responses, uint8_t *bd_addr,
					    uint8_t *page_scan_rep_mode,
					    uint8_t *reserved, uint8_t *class_of_device,
					    uint16_t *clock_offset, int8_t *rssi);
void hci_bredr_evt_extended_inquiry_result(uint8_t *bd_addr, uint8_t page_scan_rep_mode,
					   uint8_t reserved, uint8_t *class_of_device,
					   uint16_t clock_offset, int8_t rssi,
					   uint8_t *eir_data);
void hci_bredr_evt_connection_complete(uint8_t status, uint16_t handle,
				       uint8_t *bd_addr, uint8_t link_type,
				       uint8_t encryption_enabled);
void hci_bredr_evt_connection_request(uint8_t *bd_addr, uint8_t *class_of_device,
				      uint8_t link_type);
void hci_bredr_evt_disconnection_complete(uint8_t status, uint16_t handle,
					  uint8_t reason);
void hci_bredr_evt_authentication_complete(uint8_t status, uint16_t handle);
void hci_bredr_evt_remote_name_request_complete(uint8_t status, uint8_t *bd_addr,
						uint8_t *name);
void hci_bredr_evt_encryption_change(uint8_t status, uint16_t handle,
				     uint8_t encryption_enabled);
void hci_bredr_evt_change_connection_link_key_complete(uint8_t status, uint16_t handle);
void hci_bredr_evt_central_link_key_complete(uint8_t status, uint16_t handle,
					     uint8_t key_flag);
void hci_bredr_evt_read_remote_supported_features_complete(uint8_t status,
							   uint16_t handle,
							   uint8_t *features);
void hci_bredr_evt_read_remote_version_information_complete(uint8_t status,
							    uint16_t handle,
							    uint8_t version,
							    uint16_t manufacturer,
							    uint16_t subversion);
void hci_bredr_evt_qos_setup_complete(uint8_t status, uint16_t handle,
				      uint8_t flags, uint8_t service_type,
				      uint32_t token_rate, uint32_t peak_bandwidth,
				      uint32_t latency, uint32_t delay_variation);
void hci_bredr_evt_flush_occurred(uint16_t handle);
void hci_bredr_evt_role_change(uint8_t status, uint8_t *bd_addr, uint8_t new_role);
void hci_bredr_evt_mode_change(uint8_t status, uint16_t handle, uint8_t current_mode,
			       uint16_t interval);
void hci_bredr_evt_return_link_keys(uint8_t num_keys, uint8_t *bd_addr_key_pairs);
void hci_bredr_evt_pin_code_request(uint8_t *bd_addr);
void hci_bredr_evt_link_key_request(uint8_t *bd_addr);
void hci_bredr_evt_link_key_notification(uint8_t *bd_addr, uint8_t *link_key,
					 uint8_t key_type);
void hci_bredr_evt_max_slots_change(uint16_t handle, uint8_t max_slots);
void hci_bredr_evt_read_clock_offset_complete(uint8_t status, uint16_t handle,
					      uint16_t clock_offset);
void hci_bredr_evt_connection_packet_type_changed(uint8_t status, uint16_t handle,
						  uint16_t packet_type);
void hci_bredr_evt_qos_violation(uint16_t handle);
void hci_bredr_evt_page_scan_repetition_mode_change(uint8_t *bd_addr, uint8_t mode);
void hci_bredr_evt_flow_specification_complete(uint8_t status, uint16_t handle,
					       uint8_t flags, uint8_t flow_direction,
					       uint8_t service_type, uint32_t token_rate,
					       uint32_t token_bucket_size,
					       uint32_t peak_bandwidth,
					       uint32_t access_latency);
void hci_bredr_evt_read_remote_extended_features_complete(uint8_t status,
							  uint16_t handle,
							  uint8_t page_number,
							  uint8_t max_page_number,
							  uint8_t *features);
void hci_bredr_evt_synchronous_connection_complete(uint8_t status, uint16_t handle,
						   uint8_t *bd_addr, uint8_t link_type,
						   uint8_t transmission_interval,
						   uint8_t retransmission_window,
						   uint16_t rx_packet_length,
						   uint16_t tx_packet_length,
						   uint8_t air_mode);
void hci_bredr_evt_synchronous_connection_changed(uint8_t status, uint16_t handle,
						  uint8_t transmission_interval,
						  uint8_t retransmission_window,
						  uint16_t rx_packet_length,
						  uint16_t tx_packet_length);
void hci_bredr_evt_sniff_subrating(uint8_t status, uint16_t handle,
				   uint16_t max_tx_latency, uint16_t max_rx_latency,
				   uint16_t min_remote_timeout,
				   uint16_t min_local_timeout);
void hci_bredr_evt_extended_inquiry_result(uint8_t *bd_addr, uint8_t page_scan_rep_mode,
					   uint8_t reserved, uint8_t *class_of_device,
					   uint16_t clock_offset, int8_t rssi,
					   uint8_t *eir_data);
void hci_bredr_evt_encryption_key_refresh_complete(uint8_t status, uint16_t handle);
void hci_bredr_evt_io_capability_request(uint8_t *bd_addr);
void hci_bredr_evt_io_capability_response(uint8_t *bd_addr, uint8_t io_capability,
					  uint8_t oob_data_present,
					  uint8_t authentication_requirements);
void hci_bredr_evt_user_confirmation_request(uint8_t *bd_addr, uint32_t numeric_value);
void hci_bredr_evt_user_passkey_request(uint8_t *bd_addr);
void hci_bredr_evt_remote_oob_data_request(uint8_t *bd_addr);
void hci_bredr_evt_simple_pairing_complete(uint8_t status, uint8_t *bd_addr);
void hci_bredr_evt_link_supervision_timeout_changed(uint16_t handle, uint16_t timeout);
void hci_bredr_evt_enhanced_flush_complete(uint16_t handle);
void hci_bredr_evt_user_passkey_notification(uint8_t *bd_addr, uint32_t passkey);
void hci_bredr_evt_keypress_notification(uint8_t *bd_addr, uint8_t notification_type);
void hci_bredr_evt_remote_host_supported_features_notification(uint8_t *bd_addr,
							       uint8_t *features);

/*
 * HCI Command Processing Entry Point
 */
int hci_bredr_cmd_handle(uint16_t opcode, uint8_t *cmd, uint8_t cmd_len,
			 uint8_t *evt, uint8_t *evt_len);

#endif /* SUBSYS_BLUETOOTH_CONTROLLER_LL_SW_BREDR_HCI_BREDR_H_ */
