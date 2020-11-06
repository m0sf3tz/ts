#pragma once

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "stdint.h"
#include <string.h>
#include <sys/param.h>

#include "master_core.h"
#include "qcore.h"
#include "stdbool.h"
#include "stdint.h"

uint8_t*            packet_cmd_get_payload(void* pkt);
uint8_t             packet_get_type(void* ptr);
uint16_t            packet_get_size(void* pkt);
bool                packet_get_consumer_ack_req(void* pkt);
int                 packet_set_transaction_id(void* pkt, uint16_t id);
uint16_t            packet_get_transaction_id(void* pkt);
uint16_t            packet_ack_create(void* pkt, uint16_t id, uint8_t reason, uint8_t type);
uint8_t             packet_ack_get_reason(void* pkt);
int                 packet_ack_nak_set_reason(void* pkt, uint8_t reason);
int                 packet_hello_create(void* pkt, uint16_t transaction_id, uint64_t device_id, const char* device_name, uint16_t fw_version, uint8_t bricked);
uint32_t            packet_hello_get_id(void* pkt);
int                 packet_login_create(login_pkt_t* pkt, const char* name, const uint16_t temperature, const uint16_t transaction_id, const bool signIn, const uint32_t uid);
uint8_t             packet_cmd_get_type(void* pkt);
uint8_t*            packet_cmd_get_payload_data(void* pkt);
int                 packet_cmd_resp_create(void* pkt, uint16_t transaction_id, uint16_t orig_tranasaction_id, uint8_t total_packets, uint8_t packets_remaining, uint8_t cmd_status, uint8_t payload_len, void* response);
void                packet_fota_unpack(void* pkt, fota_pkt_payload_t* payload);
void                packet_fota_rsp_create(fota_ack_pkt_t* pkt, uint16_t transaction_id, uint8_t type, uint8_t status);
uint8_t*            packet_data_get_payload_data(void* pkt);
int                 packet_void_create(void* pkt, uint16_t transaction_id);
add_user_payload_t* packet_add_user_parse(void* pkt);
void                packet_sync_unpack(void* pkt, sync_pkt_payload_t* payload);
