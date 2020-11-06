#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "file_core.h"
#include "stdint.h"

/**********************************************************
 *        Used to define type of incomming packet
 *********************************************************/
#define DATA_PACKET         (0)  // Server -> Device
#define CMD_PACKET          (1)  // Server -> Device
#define INTERNAL_ACK_PACKET (2)  // Internal
#define DEVICE_ACK_PACKET   (3)  // Device -> Server
#define SERVER_ACK_PACKET   (4)  // Server -> Device
#define LOGIN_PACKET        (5)  // Device -> Server
#define HELLO_PACKET        (6)  // Device -> Server
#define CMD_RESP_PACKET     (7)  // Device -> Server
#define UNSOLICATED_PACKET  (8)  // Device -> Packet
#define ECHO_PACKET         (9)  // Device -> Packet
#define SYNC_PACKET         (10) // Server -> Device

#define FOTA_PACKET     (11) // Packet -> Device
#define FOTA_ACK_PACKET (12) // Device -> Packet (sent per 4K of incomming data)
#define VOID_PACKET     (13) // Device -> Packet (sent per 4K of incomming data)

// These are all used for tests

// Crash very early on (in chunker)
#define CRASH_PACKET (100) // Device -> Packet
// This packet is marked as requiring an ACK, but none will be generated
#define DONT_SEND_ACK_WHEN_REQUIRED (101) // Device -> Packet

#define TYPE_SIZE              (1)
#define TRANSACTION_ID_SIZE    (2)
#define SMALL_PAYLOAD_SIZE     (16)
#define MEDIUM_PAYLOAD_SIZE    (256)
#define LARGE_PLAYLOAD_SIZE    (512)
#define REASON_SIZE            (1)
#define HOST_ACK_REQ_SIZE      (1)
#define CRC_SIZE               (2)
#define PACKETS_REMAINING_SIZE (1)
#define CMD_STATUS_SIZE        (1)

#define PACKET_TYPE_OFFSET           (0)
#define PACKET_TRANSACTION_ID_OFFSET (1)
#define PACKET_HOST_ACK_REQ_OFFSET   (3)
#define PACKET_CRC_OFFSET            (4)
#define PAYLOAD_OFFSET               (6)

// Payload element offsets
#define PAYLOAD_OFFSET_ACK_NAK_REASON (0)
#define PAYLOAD_OFFSET_DEVICE_ID      (0)

#define DATA_PACKET_SIZE       (TYPE_SIZE + TRANSACTION_ID_SIZE + HOST_ACK_REQ_SIZE + CRC_SIZE + LARGE_PLAYLOAD_SIZE)
#define CMD_PACKET_SIZE        (TYPE_SIZE + TRANSACTION_ID_SIZE + HOST_ACK_REQ_SIZE + CRC_SIZE + MEDIUM_PAYLOAD_SIZE)
#define ACK_PACKET_SIZE        (TYPE_SIZE + TRANSACTION_ID_SIZE + HOST_ACK_REQ_SIZE + CRC_SIZE + SMALL_PAYLOAD_SIZE) //Same format for device/server acks
#define LOGIN_PACKET_SIZE      (TYPE_SIZE + TRANSACTION_ID_SIZE + HOST_ACK_REQ_SIZE + CRC_SIZE + MEDIUM_PAYLOAD_SIZE)
#define HELLO_PACKET_SIZE      (TYPE_SIZE + TRANSACTION_ID_SIZE + HOST_ACK_REQ_SIZE + CRC_SIZE + MEDIUM_PAYLOAD_SIZE)
#define SERVER_ACK_PACKET_SIZE (TYPE_SIZE + TRANSACTION_ID_SIZE + HOST_ACK_REQ_SIZE + CRC_SIZE + SMALL_PAYLOAD_SIZE)
#define CMD_RESP_PACKET_SIZE   (TYPE_SIZE + TRANSACTION_ID_SIZE + HOST_ACK_REQ_SIZE + CRC_SIZE + MEDIUM_PAYLOAD_SIZE)
#define ECHO_PACKET_SIZE       (TYPE_SIZE + TRANSACTION_ID_SIZE + HOST_ACK_REQ_SIZE + CRC_SIZE + MEDIUM_PAYLOAD_SIZE)
#define FOTA_PACKET_SIZE       (TYPE_SIZE + TRANSACTION_ID_SIZE + HOST_ACK_REQ_SIZE + CRC_SIZE + MEDIUM_PAYLOAD_SIZE)
#define FOTA_ACK_PACKET_SIZE   (TYPE_SIZE + TRANSACTION_ID_SIZE + HOST_ACK_REQ_SIZE + CRC_SIZE + MEDIUM_PAYLOAD_SIZE)
#define VOID_PACKET_SIZE       (TYPE_SIZE + TRANSACTION_ID_SIZE + HOST_ACK_REQ_SIZE + CRC_SIZE + MEDIUM_PAYLOAD_SIZE) /* for tests only */

//debug and test
#define DONT_SEND_ACK_WHEN_REQUIRED_SIZE (TYPE_SIZE + TRANSACTION_ID_SIZE + HOST_ACK_REQ_SIZE + CRC_SIZE + SMALL_PAYLOAD_SIZE)

#define PACKET_LEN_MAX DATA_PACKET_SIZE

#define MAX_OUTSTANDING_TCP_CORE_SEND    (8)
#define MAX_OUTSTANDING_TCP_CORE_SEND_X2 (16)
#define MAX_OUTSTANDING_TCP_CORE_RECV    MAX_OUTSTANDING_TCP_CORE_SEND_X2

#define QUEUE_LEN_ONE   (1)
#define QUEUE_MIN_SIZE  (sizeof(int))
#define DONT_WAIT_QUEUE (0)

typedef struct
{
    uint8_t cmd_type;
    uint8_t cmd_data[255];
} __attribute__((packed)) cmd_payload_t;

// WARNING, the macro below MUST match the size of the payload
// in cmd_resp_payload!
#define CMD_RESPONSE_PAYLOAD_LEN (250)
typedef struct
{
    uint8_t  cmd_status;
    uint16_t transaction_id;
    uint8_t  total_packets;
    uint8_t  packets_remaining;
    uint8_t  payload_len;
    uint8_t  resp_payload[250];
} __attribute__((packed)) cmd_resp_payload;

#define CMD_STATUS_GOOD                    (0)
#define CMD_STATUS_FAILED                  (1)
#define CMD_STATUS_FAILED_NO_CURRENT_USERS (3)
#define CMD_STATUS_FAILED_CRC              (10)
#define CMD_STATUS_UID_EXISTS              (20)

#define PAYLOAD_OFFSET_CMD_TYPE (0)
/**********************************************************
*                    ACK/NAK REASONS       
 *********************************************************/
#define ACK_GOOD             (0)
#define NAK_TCP_DOWN         (1)
#define SERVER_ACK_TIMED_OUT (2)
/**********************************************************
 *               CONSUMER ACK REQUIREMENTS
 *********************************************************/
#define CONSUMER_ACK_NOT_NEEDED (0)
#define CONSUMER_ACK_REQUIRED   (1)

#define INTERNALLY_ACKED (1)
#define PENDING_ACK      (0)

/**********************************************************
 *                  TCP PACKET RETRYS
 *********************************************************/
// For server bound packets that require a host ACK
// these variables define how many times we will send
// out our packets and not get a host ACK before we give
// up on the packet
#define PACKET_MAX_RETRIES (3)

// If we don't get consumer ACK within
// This duration, rety to send the packet
#define PACKET_RETRY_DURATION_MS (500)

// If we don't get an ACK within this
// duration, NAK this packet to the master core
#define PACKET_FAILED_DURATION_MS (3500)

/**********************************************************
 *                  MISC DEFINES
 *********************************************************/
#define QCORE_TIMEOUT (10000 / portTICK_PERIOD_MS)

/**********************************************************
 *                   PACKET TYPES
 *********************************************************/
// All the packets are guranteed to have the following format,
// They will have differnt sized payloads, but the first two
// struct members are guranteed to exist in the positions shown, hence
// general_pkt_t can be used to set the first two members safetly
typedef struct
{
    uint8_t  type;
    uint16_t transaction_id;
    uint8_t  consumer_ack_req;
    uint16_t crc;
} __attribute__((packed)) general_pkt_t;

typedef struct
{
    uint8_t  type;
    uint16_t transaction_id;
    uint8_t  consumer_ack_req;
    uint16_t crc;
    uint8_t  payload[LARGE_PLAYLOAD_SIZE];
} __attribute__((packed)) data_pkt_t;

typedef struct
{
    uint8_t  type;
    uint16_t transaction_id;
    uint8_t  consumer_ack_req;
    uint16_t crc;
    uint8_t  payload[MEDIUM_PAYLOAD_SIZE];
} __attribute__((packed)) cmd_pkt_t;

typedef struct
{
    uint8_t  type;
    uint16_t transaction_id;
    uint8_t  consumer_ack_req;
    uint16_t crc;
    uint8_t  payload[SMALL_PAYLOAD_SIZE];
} __attribute__((packed)) ack_pkt_t;

typedef struct
{
    uint8_t  type;
    uint16_t transaction_id;
    uint8_t  consumer_ack_req;
    uint16_t crc;
    uint8_t  payload[MEDIUM_PAYLOAD_SIZE];
} __attribute__((packed)) hello_pkt_t;

typedef struct
{
    uint8_t  type;
    uint16_t transaction_id;
    uint8_t  consumer_ack_req;
    uint16_t crc;
    uint8_t  payload[MEDIUM_PAYLOAD_SIZE];
} __attribute__((packed)) login_pkt_t;

typedef struct
{
    uint8_t  type;
    uint16_t transaction_id;
    uint8_t  consumer_ack_req;
    uint16_t crc;
    uint8_t  payload[MEDIUM_PAYLOAD_SIZE];
} __attribute__((packed)) cmd_resp_pkt_t;

typedef struct
{
    uint8_t  type;
    uint16_t transaction_id;
    uint8_t  consumer_ack_req;
    uint16_t crc;
    uint8_t  payload[MEDIUM_PAYLOAD_SIZE];
} __attribute__((packed)) echo_pkt_t;

typedef struct
{
    uint8_t  type;
    uint16_t transaction_id;
    uint8_t  consumer_ack_req;
    uint16_t crc;
    uint8_t  payload[MEDIUM_PAYLOAD_SIZE];
} __attribute__((packed)) fota_pkt_t;

typedef struct
{
    uint8_t  type;
    uint16_t transaction_id;
    uint8_t  consumer_ack_req;
    uint16_t crc;
    uint8_t  payload[MEDIUM_PAYLOAD_SIZE];
} __attribute__((packed)) fota_ack_pkt_t;

typedef struct
{
    uint8_t  type;
    uint16_t transaction_id;
    uint8_t  consumer_ack_req;
    uint16_t crc;
    uint8_t  payload[MEDIUM_PAYLOAD_SIZE];
} __attribute__((packed)) void_pkt_t;

/**********************************************************
 *                 SYNC related stuff
 *********************************************************/

typedef struct
{
    uint32_t crc_32;                        /* CRC of the valid "bit field" */
    uint8_t  mode;                          /* test mode (don't delete print) */
    uint8_t  valid_bit_field[MAX_EMPLOYEE]; /* valid "bitfield" - each byte corelated ot 1 "emplyee" in the spiflash */
} __attribute__((packed)) sync_pkt_payload_t;

/**********************************************************
 *                 FOTA related stuff
 *********************************************************/
//The first FOTA packet sent, has no data,
//Type is FOTA, will start the fota process
typedef struct
{
    uint32_t magic_marker;
    uint8_t  type;
    uint16_t fw_version;
    uint32_t fw_crc32;
    uint16_t fw_crc16;
    uint16_t fw_segment;
    uint16_t fw_blocks;
} __attribute__((packed)) fota_pkt_payload_t;

//FOTA response, sent
// A) Initially, to the FOTA request,
// B) ACKS to the data packets (one for every 4k)
typedef struct
{
    uint8_t type;
    uint8_t status;
} __attribute__((packed)) fota_rsp_payload_t;

/**********************************************************
 *             HELLO packet definition 
 *********************************************************/
typedef struct
{
    uint64_t deviceId;
    uint16_t fw_version;
    uint8_t  bricked;
    uint8_t  device_name[MAX_DEVICE_NAME];
} __attribute__((packed)) hello_payload_t;

/**********************************************************
 *                 PAYLOAD helpers
 *********************************************************/

// Payload difinitions

#define LOGIN_PAYLOAD_SUCCESS_SIZE         (1)
#define LOGIN_PAYLOAD_LOGIN_OR_LOGOUT_SIZE (1)
#define LOGIN_PAYLOAD_TEMPERATURE_SIZE     (2)
#define LOGIN_PAYLOAD_UID_SIZE             (4)

#define LOGIN_STRING_SIZE (MEDIUM_PAYLOAD_SIZE - LOGIN_PAYLOAD_TEMPERATURE_SIZE - LOGIN_PAYLOAD_LOGIN_OR_LOGOUT_SIZE - LOGIN_PAYLOAD_UID_SIZE)
typedef struct
{
    uint16_t temperature; // how many degrees above 0 degrees, in .1 degree increments.
    uint8_t  signInOrSignOut;
    uint32_t uid;
    uint8_t  user_name[LOGIN_STRING_SIZE];
} __attribute__((packed)) login_payload_t;

_Static_assert(sizeof(data_pkt_t) == DATA_PACKET_SIZE, "sizeof data_pkt_t not correct");
_Static_assert(sizeof(cmd_pkt_t) == CMD_PACKET_SIZE, "sizeof cmd_pkt_t not correct");
_Static_assert(sizeof(ack_pkt_t) == ACK_PACKET_SIZE, "sizeof ack_pkt_t not correct");
_Static_assert(sizeof(hello_pkt_t) == HELLO_PACKET_SIZE, "sizeof hello_pkt_t not correct");
_Static_assert(sizeof(login_pkt_t) == LOGIN_PACKET_SIZE, "sizeof login_pkt_t not correct");
_Static_assert(sizeof(login_payload_t) == MEDIUM_PAYLOAD_SIZE, "sizeof login_payload_t not correct");
_Static_assert(sizeof(cmd_resp_pkt_t) == CMD_RESP_PACKET_SIZE, "sizeof cmd_resp_pkt_t  not correct");
_Static_assert(sizeof(cmd_payload_t) == MEDIUM_PAYLOAD_SIZE, "sizeof cmd_payload struct not correct");
_Static_assert(sizeof(cmd_resp_payload) == MEDIUM_PAYLOAD_SIZE, "sizeof cmd_res_payload struct not correct");
_Static_assert(sizeof(echo_pkt_t) == ECHO_PACKET_SIZE, "sizeof echo packet struct not correct");
_Static_assert(sizeof(fota_pkt_t) == FOTA_PACKET_SIZE, "sizeof fota packet struct not correct");
_Static_assert(sizeof(void_pkt_t) == VOID_PACKET_SIZE, "sizeof fota packet struct not correct");
