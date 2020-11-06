#include "qcore.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdlib.h"
#include "system_defines.h"

#include "packet.h"
const char TAG[] = "PACKET";

uint8_t packet_get_type(void* pkt) {
    if (pkt == NULL) {
        return -1;
    }

    general_pkt_t* temp = (general_pkt_t*)pkt;
    return temp->type;
}

int packet_set_transaction_id(void* pkt, uint16_t id) {
    if (pkt == NULL) {
        return -1;
    }

    general_pkt_t* temp  = (general_pkt_t*)pkt;
    temp->transaction_id = id;
    return 0;
}

uint16_t packet_get_size(void* pkt) {
    if (pkt == NULL) {
        return -1;
    }

    general_pkt_t* temp = (general_pkt_t*)pkt;
    switch (temp->type) {
    case DATA_PACKET:
        return DATA_PACKET_SIZE;
    case CMD_PACKET:
        return CMD_PACKET_SIZE;
    case INTERNAL_ACK_PACKET:
        return ACK_PACKET_SIZE;
    case DEVICE_ACK_PACKET:
        return ACK_PACKET_SIZE;
    case LOGIN_PACKET:
        return LOGIN_PACKET_SIZE;
    case HELLO_PACKET:
        return HELLO_PACKET_SIZE;
    case CMD_RESP_PACKET:
        return CMD_RESP_PACKET_SIZE;
    case ECHO_PACKET:
        return ECHO_PACKET_SIZE;
    case FOTA_PACKET:
        return FOTA_PACKET_SIZE;
    case FOTA_ACK_PACKET:
        return FOTA_ACK_PACKET_SIZE;
    case VOID_PACKET:
        return VOID_PACKET_SIZE;
    default:
        ESP_LOGE(TAG, "Unknown argument = %d passed to packet_get_size()", temp->type);
        ASSERT(0);
        return 0; // To stop compiler complaints
    }
}

uint16_t packet_get_transaction_id(void* pkt) {
    if (pkt == NULL) {
        return -1;
    }
    general_pkt_t* temp = (general_pkt_t*)pkt;
    return temp->transaction_id;
}

// returns 1 if a consumer needs to send out an ACK
// to an outgoing packet
bool packet_get_consumer_ack_req(void* pkt) {
    if (pkt == NULL) {
        return -1;
    }

    general_pkt_t* temp = (general_pkt_t*)pkt;
    return temp->consumer_ack_req;
}

// returns -1 on fail
// returns  0 on success
int packet_ack_nak_set_reason(void* pkt, uint8_t reason) {
    if (pkt == NULL) {
        return -1;
    }

    return 0;
}

uint16_t packet_ack_create(void* pkt, uint16_t id, uint8_t reason, uint8_t type) {
    if (pkt == NULL) {
        return -1;
    }

    ack_pkt_t* temp = (ack_pkt_t*)pkt;

    temp->type             = type;
    temp->transaction_id   = id;
    temp->consumer_ack_req = 0;

    // set the reason
    temp->payload[PAYLOAD_OFFSET_ACK_NAK_REASON] = reason;

    return 0;
}

uint8_t packet_ack_get_reason(void* pkt) {
    if (pkt == NULL) {
        return -1;
    }

    ack_pkt_t* temp = (ack_pkt_t*)pkt;
    return temp->payload[PAYLOAD_OFFSET_ACK_NAK_REASON];
}

int packet_hello_create(void* pkt, uint16_t transaction_id, uint64_t device_id, const char* device_name, uint16_t fw_version, uint8_t bricked_code) {
    if (pkt == NULL || device_name == NULL) {
        ASSERT(0);
        return -1;
    }

    hello_pkt_t* temp = (hello_pkt_t*)pkt;

    temp->type             = HELLO_PACKET;
    temp->transaction_id   = transaction_id;
    temp->consumer_ack_req = CONSUMER_ACK_REQUIRED;

    hello_payload_t payload;
    payload.deviceId   = device_id;
    payload.fw_version = fw_version;
    payload.bricked    = bricked_code;

    snprintf((char*)payload.device_name, MAX_DEVICE_NAME, "%s", device_name);

    memcpy(temp->payload, &payload, sizeof(hello_payload_t));

    return transaction_id;
}

int packet_cmd_resp_create(void* pkt, uint16_t transaction_id, uint16_t orig_tranasaction_id, uint8_t total_packets, uint8_t packets_remaining, uint8_t cmd_status, uint8_t payload_len, void* response) {
    if (pkt == NULL) {
        return -1;
    }

    cmd_resp_pkt_t* temp = (cmd_resp_pkt_t*)pkt;

    temp->type             = CMD_RESP_PACKET;
    temp->transaction_id   = transaction_id;
    temp->consumer_ack_req = CONSUMER_ACK_REQUIRED;

    cmd_resp_payload cmd_response;
    cmd_response.cmd_status        = cmd_status;
    cmd_response.total_packets     = total_packets;
    cmd_response.packets_remaining = packets_remaining;
    cmd_response.transaction_id    = transaction_id;
    cmd_response.payload_len       = payload_len;

    // Copy the payload into the payloads.. payload
    if (response) {
        memcpy(cmd_response.resp_payload, response, CMD_RESPONSE_PAYLOAD_LEN);
    } else {
        memset(cmd_response.resp_payload, 0, CMD_RESPONSE_PAYLOAD_LEN);
    }

    // copy the payload (entire struct) into the cmd_resp_payload
    memcpy(temp->payload, &cmd_response, MEDIUM_PAYLOAD_SIZE);

    return transaction_id;
}

int packet_void_create(void* pkt, uint16_t transaction_id) {
    if (pkt == NULL) {
        return -1;
    }

    void_pkt_t* temp = (void_pkt_t*)pkt;

    temp->type             = VOID_PACKET;
    temp->transaction_id   = transaction_id;
    temp->consumer_ack_req = CONSUMER_ACK_REQUIRED;

    return transaction_id;
}

uint32_t packet_hello_get_id(void* pkt) {
    if (pkt == NULL) {
        return -1;
    }

    hello_pkt_t* temp = (hello_pkt_t*)pkt;

    return temp->payload[PAYLOAD_OFFSET_DEVICE_ID];
}

uint8_t packet_cmd_get_type(void* pkt) {
    if (pkt == NULL) {
        return -1;
    }
    cmd_pkt_t* temp = (cmd_pkt_t*)pkt;
    return temp->payload[PAYLOAD_OFFSET_CMD_TYPE];
}

uint8_t* packet_cmd_get_payload(void* pkt) {
    if (pkt == NULL) {
        return NULL;
    }

    cmd_pkt_t*     temp = (cmd_pkt_t*)pkt;
    cmd_payload_t* pp   = (cmd_payload_t*)temp->payload;

    return pp->cmd_data;
}

uint8_t* packet_cmd_get_payload_data(void* pkt) {
    if (pkt == NULL) {
        return NULL;
    }

    cmd_pkt_t*     temp = (cmd_pkt_t*)pkt;
    cmd_payload_t* pp   = (cmd_payload_t*)temp->payload;

    return pp->cmd_data;
}

// returns -1 on error
// returns transaction ID on sucess
int packet_login_create(login_pkt_t* pkt, const char* name, const uint16_t temperature, const uint16_t transaction_id, const bool signInOrSignOut, const uint32_t uid) {
    //TODO: add CRC

    if (pkt == NULL || name == NULL) {
        assert(0);
    }

    pkt->consumer_ack_req = CONSUMER_ACK_REQUIRED;
    pkt->type             = LOGIN_PACKET;
    pkt->transaction_id   = transaction_id;

    login_payload_t payload;
    memset(&payload, 0, sizeof(payload));

    payload.temperature     = temperature;
    payload.signInOrSignOut = signInOrSignOut;
    payload.uid             = uid;
    sprintf((char*)payload.user_name, name);

    memcpy(pkt->payload, &payload, sizeof(login_payload_t));

    return transaction_id;
}

void packet_fota_unpack(void* pkt, fota_pkt_payload_t* payload) {
    if (pkt == NULL) {
        ESP_LOGE(TAG, "NULL POINTER RXED! for PKT");
        ASSERT(0);
    }
    if (payload == NULL) {
        ESP_LOGE(TAG, "NULL POINTER RXED! for payload");
        ASSERT(0);
    }

    /* think this is wrong, shold be like sync packet */
    fota_pkt_t*         tmp = ((fota_pkt_t*)pkt)->payload;
    fota_pkt_payload_t* fp  = tmp;

    payload->magic_marker = fp->magic_marker;
    payload->type         = fp->type;
    payload->fw_version   = fp->fw_version;
    payload->fw_crc16     = fp->fw_crc16;
    payload->fw_crc32     = fp->fw_crc32;
    payload->fw_segment   = fp->fw_segment;
    payload->fw_blocks    = fp->fw_blocks;
}

void packet_sync_unpack(void* pkt, sync_pkt_payload_t* payload) {
    if (pkt == NULL) {
        ESP_LOGE(TAG, "NULL POINTER RXED! for PKT");
        ASSERT(0);
    }
    if (payload == NULL) {
        ESP_LOGE(TAG, "NULL POINTER RXED! for payload");
        ASSERT(0);
    }

    sync_pkt_payload_t* fp = (sync_pkt_payload_t*)pkt;

    payload->crc_32 = fp->crc_32;
    payload->mode   = fp->mode;
    memcpy(payload->valid_bit_field, fp->valid_bit_field, MAX_EMPLOYEE);
}

void packet_fota_rsp_create(fota_ack_pkt_t* pkt, uint16_t transaction_id, uint8_t type, uint8_t status) {
    if (pkt == NULL) {
        ESP_LOGE(TAG, "NULL POINTER RXED! for PKT");
    }

    pkt->consumer_ack_req = CONSUMER_ACK_REQUIRED;
    pkt->type             = FOTA_ACK_PACKET;
    pkt->transaction_id   = transaction_id;

    fota_rsp_payload_t  payload;
    fota_rsp_payload_t* payload_ptr;
    memset(&payload, 0, sizeof(payload));

    payload.type   = type;
    payload.status = status;
    memcpy(pkt->payload, &payload, sizeof(fota_rsp_payload_t));

    return transaction_id;
}

uint8_t* packet_data_get_payload_data(void* pkt) {
    if (pkt == NULL) {
        return NULL;
    }
    data_pkt_t* temp = (data_pkt_t*)pkt;
    return temp->payload;
}

add_user_payload_t* packet_add_user_parse(void* pkt) {
    if (pkt == NULL) {
        ESP_LOGE(TAG, "Rxed a NULL pkt");
    }

    return (add_user_payload_t*)packet_cmd_get_payload_data(pkt);
}
