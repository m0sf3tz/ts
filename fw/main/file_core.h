#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* define reqests to file core */
#define FILE_ADD_USER         (0)
#define FILE_MATCH_ID_TO_USER (1)
#define FILE_FORMAT_FLASH     (2)
#define FILE_PRINT_USERS      (3)
#define FILE_DELETE_USER      (4)
#define FILE_DELETE_ALL_USERS (6)
#define FILE_GET_FREE_ID      (7)

/* define responses from file core */
#define FILE_RET_OK             (0)
#define FILE_RET_FAIL           (10)
#define FILE_RET_MEM_FULL       (11)
#define FILE_RET_USER_NOT_EXIST (12)
#define FILE_RET_USER_EXISTS    (13)
#define FILE_MEM_EMPTY          (14)

/* employee tracking */
#define MAX_NAME_LEN_PLUS_NULL (49)
#define MAX_EMPLOYEE           (128)

#define WEAR_PARTITION_SIZE (0xe1000)

/* NVS related stuff */
#define ITEM_GOOD           (0)
#define ITEM_NOT_INITILIZED (1)
#define ITEM_OTHER_PROBLEM  (2)
#define ITEM_CANT_GET       (3)
#define ITEM_CANT_SET       (4)

#define NVS_DEVICE_ID     (0)
#define NVS_FW_COOKIE     (1)
#define NVS_JOURNAL       (2)
#define NVS_JOURNAL_VALID (3)
#define NVS_DEVICE_NAME   (4)
#define NVS_SSID_NAME     (5)
#define NVS_SSID_PW       (6)
#define NVS_IP            (7)
#define NVS_PORT          (8)
#define NVS_BRICKED       (9)

/* Max len for device name */
#define MAX_DEVICE_NAME (50)
#define MAX_IP_LEN      (100)
#define MAX_SSID_LEN    (100)
#define MAX_PW_LEN      (100)

#define FOTA_COOKIE_NO_PENDING_NEW_FW (0)
#define FOTA_COOKIE_RUNNING_NEW_FW    (1)

// Bricked codes
#define NOT_BRICKED (0)
#define UNKNOWN_LOGIN_BRICKED (1)
#define FAILED_TO_DELETE_THUMB_BRICKED (2)

typedef struct
{
    uint16_t id;
    uint32_t uid;
    char     name[MAX_NAME_LEN_PLUS_NULL];
} __attribute__((packed)) employee_id_t;

typedef struct
{
    uint32_t  command;
    char*     name;
    uint16_t  id;
    uint16_t* next_free_id;
    uint32_t  uid; /* Global ID */
} commandQ_file_t;

// Used for letting the rest of the system know file-core is ready
extern int           fileCoreReady;
extern QueueHandle_t fileCommandQ, fileCommandQ_res;
extern uint8_t       file_core_total_users;
extern employee_id_t file_core_all_users_arr[MAX_EMPLOYEE];
extern bool          file_core_all_users_arr_valid[MAX_EMPLOYEE];

void file_thread(void* ptr);
int  file_thread_gate(commandQ_file_t cmd);
int  file_core_get_username(char* buf, const uint16_t id, uint32_t* uid);
void file_core_spawner();
void file_core_init_freertos_objects();
void file_core_mutex_take();
void file_core_mutex_give();
bool file_core_user_exists(uint32_t uid, uint16_t* id);

int file_core_get(int item, void* data);
int file_core_set(int item, void* data);
uint8_t file_core_is_bricked();

void file_core_clear_journal();
void file_core_set_journal(uint16_t uid);
bool file_core_get_journal(uint16_t* uid);

int  verify_nvs_required_items();
void file_core_print_details();
void lcd_boot_message();
#define FILE_ARR_MAX_MUTEX_WAIT (5000 / portTICK_PERIOD_MS)
