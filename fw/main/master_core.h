#pragma once
#include "file_core.h"
#include "system_defines.h"

void     master_core_spawner();
uint16_t create_transaction_id();
void     set_fota_underway(bool underway);
void     send_to_tcp_core(void* packet);
bool     get_fota_underway();
void     delay_boot();
void     is_device_bricked();

typedef enum {
    MASTER_CORE_NOT_REGISTERED,
    MASTER_CORE_REGISTERED
} master_core_reg_state_e;

typedef enum {
    INFLIGHT_INCREMENT,
    INFLIGHT_DECREMENT
} master_core_in_flight_e;

typedef enum {
    INCREMENT_OUTSTANDING_COMMANDS,
    DECREMENT_OUTSTANDING_COMMANDS,
} master_core_outstanding_commands_e;

// ADD USER
typedef struct
{
    uint32_t uid;
    uint8_t  replace; //if UID already exists, should we relace?
    char     name[MAX_NAME_LEN_PLUS_NULL];
} __attribute__((packed)) add_user_payload_t;

/**********************************************************
*                      GLOBALS    
*********************************************************/

extern QueueHandle_t master_to_fota_q;
extern bool          disable_next_ack;

/**********************************************************
*                      GENERAL
*********************************************************/
#define MANUFACTURING_DEVICE_ID           (0xFFFFFFFF)
#define SYNC_TIME_BETWEEN_CHAINED_COMMAND (10000 / portTICK_PERIOD_MS)
#define MASTER_CORE_MAX_MUTEX_WAIT        (10000 / portTICK_PERIOD_MS)

/**********************************************************
*                      CMDS    
*********************************************************/
#define NOT_USED                   (0)
#define DELETE_ALL_USERS_CMD       (1)
#define GET_ALL_USERS_CMD          (2)
#define DELETE_SPECIFIC_USER_CMD   (3)
#define DISCONNECT_CMD             (4)
#define ADD_USER_CMD               (6) /* Backend calls this "push" user-  keeping it as "add' user in FW land so I dont' haven to update enverything */
#define NOT_USE_1                  (7)
#define GET_ALL_USERS_AND_SYNC_CMD (8)
#define SYNC_COMMAND               (9)
/* USED BY BACKEND (10-13) */
#define DISPLAY_MSG_LCD            (14)

// test only
#define ECHO_CMD                       (100)
#define TIME_OUT_NEXT_PACKET           (101)
#define VOID_CMD                       (102)
#define SEND_MULTI_PART_RSP            (103)
#define SEND_MULTI_PART_RSP_FAIL       (104)
#define ADD_USER_TO_FLASH              (105)
#define DELETE_SPECIFIC_USER_CMD_FLASH (106)
#define SEND_TEST_LOGIN_PACKET         (107)
#define SET_DEVICE_ID_TEST             (108)
#define GET_DEVICE_ID_TEST             (109)
#define ACK_STRESS_TEST                (110)

// used for multi-part test
#define OFFSET_PAYLOAD_OFFSET_PACKET_COUNT_MULTI_PART_TEST (0)

#define MASTER_TO_FOTA_DEPTH      (4)
#define MASTER_TO_FOTA_Q_TIME_OUT (2000 / portTICK_PERIOD_MS)
#define MASTER_TIMEOUT            (10000 / portTICK_PERIOD_MS)

#define ADD_USER_FLASH            (0)
#define ADD_USER_FLASH_PLUS_PRINT (1)

#define DEL_USER_FLASH            (0)
#define DEL_USER_FLASH_PLUS_PRINT (1)

#define SYNC_NOT_UNDERWAY (0)
#define SYNC_UNDERWAY     (1)

#define NULL_INTERNAL_ID           (0xFFFF)
#define SYNC_DELETE_USER_BIT_FIELD (100) /* this is shared between go and c */
#define SYNC_USER_EXISTS_BIT_FIELD (200) /* this is shared between go and c */

#define SYNC_NORMAL_MODE (0)
#define SYNC_TEST_MODE   (1)
#define TIME_TO_SUICIDE  (250)
