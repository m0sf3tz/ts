#include "esp_system.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <errno.h>

#include "file_core.h"
#include "fota_task.h" //fw-version
#include "lcd.h"
#include "system_defines.h"

#include "nvs.h"
#include "nvs_flash.h"

/**********************************************************
*              MASTER CORE STATIC VARIABLES
**********************************************************/
static const char* base_path = "/spiflash"; // Base path
static const char  TAG[30]   = "FILE_CORE";
// Handle of the wear levelling library instance
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
// local mutexes
static SemaphoreHandle_t fileCommandMutex;
static SemaphoreHandle_t fileUserArrMutex;
static SemaphoreHandle_t nvs_sem;

/**********************************************************
*              FILE CORE GLOBAL VARIABLES
**********************************************************/
QueueHandle_t fileCommandQ;
QueueHandle_t fileCommandQ_res;
uint8_t       file_core_total_users;
employee_id_t file_core_all_users_arr[MAX_EMPLOYEE];
bool          file_core_all_users_arr_valid[MAX_EMPLOYEE];
int           fileCoreReady;

/**********************************************************
*                  FILE CORE FUNCTIONS
**********************************************************/
static int get_free_user_id(uint16_t*);

int file_thread_gate(commandQ_file_t cmd) {
    int ret;
    xSemaphoreTake(fileCommandMutex, portMAX_DELAY);
    xQueueSend(fileCommandQ, &cmd, 0);
    xQueueReceive(fileCommandQ_res, &ret, portMAX_DELAY);
    xSemaphoreGive(fileCommandMutex);
    return ret;
}

static void mount_spiff() {
    ESP_LOGI(TAG, "Mounting FAT filesystem");
    // To mount device we need name of device partition, define base_path
    // and allow format partition in case if it is new one and was not formated before
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files              = 4,
        .format_if_mount_failed = true,
        .allocation_unit_size   = CONFIG_WL_SECTOR_SIZE
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount(base_path, "storage", &mount_config, &s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        assert(0);
        //TODO: Gracefully handle this....
    }
}

bool file_core_user_exists(uint32_t uid, uint16_t* id) {
    if (id == NULL) {
        assert(0);
    }

    bool ret = false;
    file_core_mutex_take();
    int i;

    for (i = 0; i < MAX_EMPLOYEE; i++) {
        if (file_core_all_users_arr_valid[i]) {
            if (file_core_all_users_arr[i].uid == uid) {
                ret = true;
                break;
            }
        }
    }

    // set the slot
    *id = i;

    file_core_mutex_give();
    if (ret) {
        ESP_LOGI(TAG, "user with UID  %u exists (slot %hu)", uid, i);
    } else {
        ESP_LOGI(TAG, "user with UID %u does not exist", uid);
    }
    return ret;
}

int file_core_get_username(char* buf, const uint16_t id, uint32_t* uid) {
    if (buf == NULL || uid == NULL) {
        assert(0);
    }

    if (file_core_all_users_arr_valid[id]) {
        memcpy(buf, file_core_all_users_arr[id].name, MAX_NAME_LEN_PLUS_NULL);
        *uid = file_core_all_users_arr[id].uid;
        return FILE_RET_OK;
    } else {
        ESP_LOGE(TAG, "No valid ID stored for id %hu", id);
        return FILE_RET_FAIL;
    }
}

// Function will loop through all posible users IDs
// And stop at the first one that does not exist
static int add_user(commandQ_file_t* cmd) {
    ESP_LOGI(TAG, "Adding user %s, uid %u id %hu", cmd->name, cmd->uid, cmd->id);
    int           ret;
    uint16_t      id;
    char          fileName[30];
    employee_id_t temp;

    id = cmd->id;

    if (id > MAX_EMPLOYEE) {
        ESP_LOGE(TAG, "Free ID GREATED than max employee, id = %d", id);
        return FILE_RET_FAIL;
    }

    if (id > MAX_EMPLOYEE) {
        ESP_LOGE(TAG, "ID out of range: ID == %hu", id);
        return FILE_RET_FAIL;
    }

    if (strlen(cmd->name) > MAX_NAME_LEN_PLUS_NULL - 1) //strlen does not count NULL char
    {
        ESP_LOGE(TAG, "Name was too long");
        return FILE_RET_FAIL;
    }

    sprintf(fileName, "/spiflash/id_%hu", id);

    ret = access(fileName, F_OK);

    if (ret == 0) {
        ESP_LOGE(TAG, "Could not open file!");
        ASSERT(0);
    }
    if (errno != ENOENT) {
        ESP_LOGE(TAG, "Errno not ENOENT as expected");
        ASSERT(0);
    }

    sprintf(fileName, "/spiflash/id_%hu", id);
    ESP_LOGI(TAG, "Opening file %s to update", fileName);
    FILE* z = fopen(fileName, "wb");

    if (z == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return FILE_RET_FAIL;
    }

    // Fomat the user ID + Name to write to flash
    memset(&temp, 0, sizeof(employee_id_t));
    temp.id  = id;
    temp.uid = cmd->uid;
    sprintf(temp.name, cmd->name);

    ESP_LOGI(TAG, "Updating Flash for user %d, name %s, uid %u", temp.id, temp.name, temp.uid);
    ret = fwrite(&temp, 1, sizeof(employee_id_t), z);

    fclose(z);

    if (ret == 0) {
        ESP_LOGE(TAG, "Failed write to file");
        return FILE_RET_FAIL;
    }

    if (ret != sizeof(employee_id_t)) {
        ESP_LOGE(TAG, "Only partially wrote to file, %d bytes", ret);
        return FILE_RET_FAIL;
    }

    ESP_LOGI(TAG, "Done adding userid = %d!", id);
    return FILE_RET_OK;
}

static int delete_user(commandQ_file_t* cmd) {
    ESP_LOGI(TAG, "Deleting user %hu ", cmd->id);
    int  ret;
    char fileName[30];

    if (cmd->id > MAX_EMPLOYEE) {
        ESP_LOGE(TAG, "ID out of range: ID == %hu", cmd->id);
        return FILE_RET_FAIL;
    }

    // If a user exists, a file will exist in the form of
    // "id_x", where X is the users ID
    // IE: id_34 for person 34 etc
    sprintf(fileName, "/spiflash/id_%hu", cmd->id);
    ret = access(fileName, F_OK); //checks to see if file exists

    if (ret == 0) {
        remove(fileName);
        return FILE_RET_OK;
    }

    if (errno == ENOENT) {
        ESP_LOGE(TAG, "User %d does not exist, can't delete", cmd->id);
        return FILE_RET_USER_NOT_EXIST;
    }

    ESP_LOGE(TAG, "Unexpected errno - %d", errno);
    return FILE_RET_FAIL;
}

// loads list of users into memory from flash
static int load_users() {
    ESP_LOGI(TAG, "building list of users from memory");
    char     employee[64];
    char     fileName[30];
    int      ret;
    uint16_t i;

    // Reset structures
    file_core_total_users = 0;
    for (i = 0; i < MAX_EMPLOYEE; i++) {
        memset(&file_core_all_users_arr[i], 0, sizeof(employee_id_t));
        file_core_all_users_arr_valid[i] = false;
    }

    for (i = 0; i < MAX_EMPLOYEE; i++) {
        sprintf(fileName, "/spiflash/id_%hu", i);
        ret = access(fileName, F_OK);
        if (ret == 0) {
            FILE* f = fopen(fileName, "rb");
            if (f == NULL) {
                ESP_LOGE(TAG, "Failed to open file for reading");
                return FILE_RET_FAIL;
            }
            fread(employee, 1, sizeof(employee_id_t), f);
            employee_id_t e;

            memset((void*)&e, 0, sizeof(employee_id_t));
            memcpy((void*)&e, (const void*)employee, sizeof(employee_id_t));

            // this times out some tests..
            //ESP_LOGI(TAG, "ID (slot): %hu, Name: %s, (global)uid=%u", e.id, e.name, e.uid);
            
            memcpy(file_core_all_users_arr[i].name, &e.name, MAX_NAME_LEN_PLUS_NULL);
            file_core_all_users_arr[i].uid   = e.uid;
            file_core_all_users_arr_valid[i] = true;

            // increment total users
            file_core_total_users++;

            fclose(f);
            continue;
        }
        if (errno == ENOENT) {
            continue;
        }
        // should not get here
        ESP_LOGE(TAG, "Strang errno: %d:  giving up", errno);
        return FILE_RET_FAIL;
    }
    return FILE_RET_OK;
}

static int print_users() {
    ESP_LOGI(TAG, "Printing users");

    char     employee[64];
    char     fileName[30];
    int      ret;
    uint16_t i;

    for (i = 0; i < MAX_EMPLOYEE; i++) {
        sprintf(fileName, "/spiflash/id_%hu", i);
        ret = access(fileName, F_OK);
        if (ret == 0) {
            FILE* f = fopen(fileName, "rb");
            if (f == NULL) {
                ESP_LOGE(TAG, "Failed to open file for reading");
                return FILE_RET_FAIL;
            }
            fgets(employee, sizeof(employee_id_t), f);
            employee_id_t e;

            memset((void*)&e, 0, sizeof(employee_id_t));
            memcpy((void*)&e, (const void*)employee, sizeof(employee_id_t));

            printf("\nEmployee ID: %hu\n", e.id);
            printf("Employee Name: %s\n\n", e.name);

            fclose(f);
            continue;
        }
        if (errno == ENOENT) {
            continue;
        }
        // should not get here
        ESP_LOGE(TAG, "Strang errno: %d:  giving up", errno);
        return FILE_RET_FAIL;
    }
    return FILE_RET_OK;
}

static esp_err_t format_flash() {
    ESP_LOGI(TAG, "Formatting file system");
    const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    if (partition == NULL) {
        ESP_LOGE(TAG, "Failed to find partition");
        return FILE_RET_FAIL;
    }

    if (ESP_OK != esp_partition_erase_range(partition, 0, WEAR_PARTITION_SIZE)) {
        ESP_LOGE(TAG, "Failed to erase partition");
        return FILE_RET_FAIL;
    }
    return FILE_RET_OK;
}

static int delete_all_users() {
    ESP_LOGI(TAG, "Deleting users");

    char      fileName[30];
    int       ret;
    uint16_t  i;
    esp_err_t err;

    err = esp_vfs_fat_spiflash_unmount(base_path, s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FAILED TO UNREGISTER FLASH FATFS (%s)", esp_err_to_name(err));
        ASSERT(0);
    }

    if (format_flash()) {
        ESP_LOGE(TAG, "FAILED TO FORMAT FLASH");
        ASSERT(0);
    }

    // will assert internall if failed
    mount_spiff();

    return FILE_RET_OK;
}

static int get_free_user_id(uint16_t* id) {
    int      ret;
    uint16_t i;
    char     fileName[30];

    if (!id) {
        ASSERT(0);
    }

    for (i = 0; i < MAX_EMPLOYEE; i++) {
        sprintf(fileName, "/spiflash/id_%hu", i);
        ret = access(fileName, F_OK);

        if (ret == 0) {
            // This user already exists
            continue;
        }
        if (errno == ENOENT) {
            ESP_LOGI(TAG, "Next free id = %d chosen", i);
            *id = i;
            return 0;
        }
    }
    ESP_LOGE(TAG, "Ran out of free users");
    return FILE_RET_MEM_FULL;
}

static int get_user_from_id(uint16_t id, char* name_buff) {
    if (name_buff == NULL) {
        ASSERT(0);
    }

    char employee[64];
    char fileName[30];
    int  ret;

    sprintf(fileName, "/spiflash/id_%hu", id);
    ret = access(fileName, F_OK);
    if (ret == 0) {
        FILE* f = fopen(fileName, "rb");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open file for reading");
            fclose(f);
            return FILE_RET_FAIL;
        }

        fgets(employee, sizeof(employee_id_t), f);
        employee_id_t e;

        memset((void*)&e, 0, sizeof(employee_id_t));
        memcpy((void*)&e, (const void*)employee, sizeof(employee_id_t));

        memset(name_buff, 0, MAX_NAME_LEN_PLUS_NULL);
        sprintf(name_buff, "%s", e.name);

        fclose(f);

        return FILE_RET_OK;
    } else if (errno == ENOENT) {
        ESP_LOGW(TAG, "WARNING - Could not find user to match!");
        return FILE_RET_FAIL;
    }
    // should not get here
    ESP_LOGE(TAG, "Strang errno: %d, while trying to read user_id %hu", errno, id);
    return FILE_RET_FAIL;
}

/* file_core_mutex_give/take are global, anytime someone wants to touch the global 
 * users arr they need to take these mutexs */
void file_core_mutex_take() {
    if (pdTRUE != xSemaphoreTake(fileUserArrMutex, FILE_ARR_MAX_MUTEX_WAIT)) {
        ESP_LOGE(TAG, "FAILED TO GET FILE_ARR_MUTEX!");
        ASSERT(0);
    }
}

void file_core_mutex_give() {
    xSemaphoreGive(fileUserArrMutex);
}

void file_thread(void* ptr) {
    commandQ_file_t commandQ_cmd;

    //mount the FS
    mount_spiff();

    // build the in memory list of users.
    load_users();

    //let the rest of the system know we file-core is ready
    fileCoreReady = 1;

    for (;;) {
        int ret;

        // Wait for command
        BaseType_t xStatus = xQueueReceive(fileCommandQ, &commandQ_cmd, portMAX_DELAY);
        if (xStatus == 0) {
            ESP_LOGE(TAG, "Failed to create Queue..");
        }

        switch (commandQ_cmd.command) {
        case FILE_ADD_USER:
            file_core_mutex_take();
            ret = add_user(&commandQ_cmd);
            load_users();
            file_core_mutex_give();
            xQueueSend(fileCommandQ_res, &ret, 0);
            break;
        case FILE_FORMAT_FLASH:
            file_core_mutex_take();
            ret = format_flash();
            file_core_mutex_give();
            xQueueSend(fileCommandQ_res, &ret, 0);
            break;
        case FILE_PRINT_USERS:
            file_core_mutex_take();
            ret = print_users();
            file_core_mutex_give();
            xQueueSend(fileCommandQ_res, &ret, 0);
            break;
        case FILE_DELETE_USER:
            file_core_mutex_take();
            ret = delete_user(&commandQ_cmd);
            load_users();
            file_core_mutex_give();
            xQueueSend(fileCommandQ_res, &ret, 0);
            break;
        case FILE_DELETE_ALL_USERS:
            file_core_mutex_take();
            ret = delete_all_users();
            load_users();
            file_core_mutex_give();
            xQueueSend(fileCommandQ_res, &ret, 0);
            break;
        case FILE_GET_FREE_ID:
            file_core_mutex_take();
            ret = get_free_user_id(commandQ_cmd.next_free_id);
            file_core_mutex_give();
            xQueueSend(fileCommandQ_res, &ret, 0);
            break;
        case FILE_MATCH_ID_TO_USER:
            file_core_mutex_take();
            ret = get_user_from_id(commandQ_cmd.id, commandQ_cmd.name);
            file_core_mutex_give();
            xQueueSend(fileCommandQ_res, &ret, 0);
            break;
        }
    }
}

void file_core_spawner() {
    BaseType_t rc;
    rc = xTaskCreate(file_thread,
                     "file_core",
                     2 * 4096,
                     NULL,
                     4,
                     NULL);

    if (rc != pdPASS) {
        assert(0);
    }
}

void file_core_init_freertos_objects() {
    fileCommandQ     = xQueueCreate(1, sizeof(commandQ_file_t));
    fileCommandQ_res = xQueueCreate(1, sizeof(int));
    fileCommandMutex = xSemaphoreCreateMutex();
    fileUserArrMutex = xSemaphoreCreateMutex();
    nvs_sem          = xSemaphoreCreateMutex();
}

int file_core_set(int item, void* data) {
    if (pdTRUE != xSemaphoreTake(nvs_sem, FILE_ARR_MAX_MUTEX_WAIT)) {
        ESP_LOGE(TAG, "FAILED TO TAKE NVS_MUTEX!");
        ASSERT(0);
    }

    if (!data) {
        ESP_LOGE(TAG, "DATA == NULL");
        ASSERT(0);
    }

    nvs_handle_t my_handle;
    esp_err_t    err = nvs_open("nvs", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        xSemaphoreGive(nvs_sem);
        return ITEM_CANT_SET;
    } else {
        switch (item) {
        case (NVS_DEVICE_ID):
            printf("Updating deviceId in NVS ... \n");
            err = nvs_set_u64(my_handle, "device_id", *(uint64_t*)(data));
            break;
        case (NVS_FW_COOKIE):
            printf("Updating new_fw_cookie in NVS ... \n");
            err = nvs_set_u8(my_handle, "new_fw_cookie", *(uint8_t*)(data));
            break;
        case (NVS_JOURNAL):
            printf("Updating journal in NVS ... \n");
            err = nvs_set_u16(my_handle, "journal", *(uint16_t*)(data));
            break;
        case (NVS_JOURNAL_VALID):
            printf("Updating journal_valid in NVS ... \n");
            err = nvs_set_u16(my_handle, "valid", *(uint16_t*)(data));
            break;
        case (NVS_DEVICE_NAME):
            printf("Setting device_name in NVS ... \n");
            err = nvs_set_str(my_handle, "device_name", (const char*)(data));
            break;
        case (NVS_SSID_NAME):
            printf("Setting ssid_name in NVS ... \n");
            err = nvs_set_str(my_handle, "ssid_name", (const char*)(data));
            break;
        case (NVS_SSID_PW):
            printf("Setting ssid_pw in NVS ... \n");
            err = nvs_set_str(my_handle, "ssid_pw", (const char*)(data));
            break;
        case (NVS_IP):
            printf("Setting ip in NVS ... \n");
            err = nvs_set_str(my_handle, "ip", (const char*)(data));
            break;
        case (NVS_PORT):
            printf("Setting port in NVS ... \n");
            err = nvs_set_u16(my_handle, "port", *(uint16_t*)(data));
            break;
        case (NVS_BRICKED):
            printf("setting bricked code from nvs... \n");
            err = nvs_set_u8(my_handle, "bricked", *(uint8_t*)(data));
            break;
        default:
            ESP_LOGE(TAG, "Unknown item = %d", item);
            ASSERT(0);
        }

        // Commit written value.
        // After setting any values, nvs_commit() must be called to ensure changes are written
        // to flash storage. Implementations may write to storage at other times,
        // but this is not guaranteed.
        if (err == ESP_OK) {
            printf("Committing updates in NVS ... \n");
            err = nvs_commit(my_handle);
            if (err != ESP_OK) {
                printf("err == %s", esp_err_to_name(err));
            } else {
                printf("no issues commiting\n");
            }
        }
        // Close
        xSemaphoreGive(nvs_sem);
        nvs_close(my_handle);
        return ITEM_GOOD;
    }
}

int file_core_get(int item, void* data) {
    int    status;
    size_t size_of_name = 0;

    if (!data) {
        ESP_LOGE(TAG, "DATA == NULL");
        ASSERT(0);
    }

    if (pdTRUE != xSemaphoreTake(nvs_sem, FILE_ARR_MAX_MUTEX_WAIT)) {
        ESP_LOGE(TAG, "FAILED TO TAKE NVS_MUTEX!");
        ASSERT(0);
    }

    printf("\n");
    printf("Opening Non-Volatile Storage (NVS) handle... ");
    nvs_handle_t my_handle;
    esp_err_t    err = nvs_open("nvs", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        status = ITEM_OTHER_PROBLEM;
        xSemaphoreGive(nvs_sem);
        return;
    } else {
        // Read
        switch (item) {
        case (NVS_DEVICE_ID):
            printf("Reading deviceId in NVS ... \n");
            err = nvs_get_u64(my_handle, "device_id", (uint64_t*)(data));
            break;
        case (NVS_FW_COOKIE):
            printf("Reading new_fw_cookie in NVS ... \n");
            err = nvs_get_u8(my_handle, "new_fw_cookie", (uint8_t*)(data));
            break;
        case (NVS_JOURNAL):
            printf("Reading journal in NVS ... \n");
            err = nvs_get_u16(my_handle, "journal", (uint16_t*)(data));
            break;
        case (NVS_JOURNAL_VALID):
            printf("Reading journal_valid in NVS ... \n");
            err = nvs_get_u16(my_handle, "valid", (uint16_t*)(data));
            break;
        case (NVS_DEVICE_NAME):
            printf("Reading size of device_Name in NVS ... \n");
            err = nvs_get_str(my_handle, "device_name", NULL, &size_of_name);
            if (err != ESP_OK) {
                printf("Failed to get name for device \n");
                break;
            }
            printf("Reading device_Name in NVS ... \n");
            err = nvs_get_str(my_handle, "device_name", (const char*)(data), &size_of_name);
            printf("Reading device_Name in NVS ...%s \n", (char*)data);
            break;
        case (NVS_SSID_NAME):
            printf("Reading size of ssid_name in NVS ... \n");
            err = nvs_get_str(my_handle, "ssid_name", NULL, &size_of_name);
            if (err != ESP_OK) {
                printf("Failed to get ssid_name for device \n");
                break;
            }
            printf("Reading ssid_name in NVS ... \n");
            err = nvs_get_str(my_handle, "ssid_name", (const char*)(data), &size_of_name);
            printf("ssid_name = ...%s \n", (char*)data);
            break;
        case (NVS_SSID_PW):
            printf("Reading size of ssid_pw in NVS ... \n");
            err = nvs_get_str(my_handle, "ssid_pw", NULL, &size_of_name);
            if (err != ESP_OK) {
                printf("Failed to get ssid_pw for device \n");
                break;
            }
            printf("Reading ssid_pwd in NVS ... \n");
            err = nvs_get_str(my_handle, "ssid_pw", (const char*)(data), &size_of_name);
            printf("ssid_pw = ...%s \n", (char*)data);
            break;
        case (NVS_IP):
            printf("Reading size of ip in NVS ... \n");
            err = nvs_get_str(my_handle, "ip", NULL, &size_of_name);
            if (err != ESP_OK) {
                printf("Failed to get ip for device \n");
                break;
            }
            printf("Reading ip in NVS ... \n");
            err = nvs_get_str(my_handle, "ip", (const char*)(data), &size_of_name);
            printf("ip = ...%s \n", (char*)data);
            break;
        case (NVS_PORT):
            printf("Reading port in NVS ... \n");
            err = nvs_get_u16(my_handle, "port", (uint16_t*)(data));
            break;
        case (NVS_BRICKED):
            printf("Reading bricked code from nvs... \n");
            err = nvs_get_u8(my_handle, "bricked", (uint8_t*)(data));
            break;
        default:
            ESP_LOGE(TAG, "Unknown item = %d \n", item);
            ASSERT(0);
        }
        switch (err) {
        case ESP_OK:
            printf("Done\n");
            status = ITEM_GOOD;
            nvs_close(my_handle);
            xSemaphoreGive(nvs_sem);
            return status;
        case ESP_ERR_NVS_NOT_FOUND:
            status = ITEM_NOT_INITILIZED;
            nvs_close(my_handle);
            xSemaphoreGive(nvs_sem);
            return status;
        default:
            printf("Error (%s) reading!\n", esp_err_to_name(err));
            ASSERT(0);
        }
    }
    printf("Shouldnot get here");
    ASSERT(0);
    return 0;
}

int verify_nvs_required_items() {
    uint64_t device_id;
    int      ret_deviceid = file_core_get(NVS_DEVICE_ID, &device_id);

    char ssid_name[MAX_SSID_LEN];
    int  ret_ssid_name = file_core_get(NVS_SSID_NAME, ssid_name);

    char ssid_pw[MAX_PW_LEN];
    int  ret_ssid_pw = file_core_get(NVS_SSID_PW, ssid_pw);

    char ip[MAX_IP_LEN];
    int  ret_ip = file_core_get(NVS_IP, ip);

    uint16_t port;
    int      ret_port = file_core_get(NVS_PORT, &port);

    char device_name[MAX_DEVICE_NAME];
    int  ret_device_name = file_core_get(NVS_DEVICE_NAME, device_name);

    //check if this is the first time we are runnig (the BRICKED value will not be set..)
    uint8_t bricked;
    int  ret_bricked = file_core_get(NVS_BRICKED, &bricked);
    
      ESP_LOGI(TAG, "Bricked = %hhu, err = %d",bricked, ret_bricked);
    if(ret_bricked ==  ITEM_NOT_INITILIZED) {
      ESP_LOGI(TAG, "First time running device, setting bricked code to 0");
      bricked = 0;
      ret_bricked = file_core_set(NVS_BRICKED, &bricked);
      if (ret_bricked != ESP_OK){
        ESP_LOGE(TAG, "Could not set Value of briked on the first run?!");
        ASSERT(0);
      }
    }

    if (ret_deviceid || ret_ssid_name || ret_ssid_pw || ret_ip || ret_port || ret_device_name) {
        return 0;
    } else {
        return 1;
    }
}



uint8_t file_core_is_bricked(){
    uint8_t bricked;
    file_core_get(NVS_BRICKED, &bricked);
    return bricked;
}

void lcd_boot_message() {

    char helper_string[100];
    memset(helper_string, 0, 100);

    uint64_t device_id;
    int      ret_deviceid = file_core_get(NVS_DEVICE_ID, &device_id);

    char ssid_name[MAX_SSID_LEN];
    int  ret_ssid_name = file_core_get(NVS_SSID_NAME, ssid_name);

    char ssid_pw[MAX_PW_LEN];
    int  ret_ssid_pw = file_core_get(NVS_SSID_PW, ssid_pw);

    char ip[MAX_IP_LEN];
    int  ret_ip = file_core_get(NVS_IP, ip);

    uint16_t port;
    int      ret_port = file_core_get(NVS_PORT, &port);

    char device_name[MAX_DEVICE_NAME];
    int  ret_device_name = file_core_get(NVS_DEVICE_NAME, device_name);

    print_lcd_api((uint8_t*)"Device ID:");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    snprintf(helper_string, 100, "%u", (uint32_t)device_id);
    print_lcd_api((uint8_t*)helper_string);
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    print_lcd_api((uint8_t*)"SSID:");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    snprintf(helper_string, 100, "%s", ssid_name);
    print_lcd_api((uint8_t*)helper_string);
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    print_lcd_api((uint8_t*)"Server IP:");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    snprintf(helper_string, 100, "%s", ip);
    print_lcd_api((uint8_t*)helper_string);
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    print_lcd_api((uint8_t*)"Server PORT:");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    snprintf(helper_string, 100, "%hu", (uint16_t)port);
    print_lcd_api((uint8_t*)helper_string);
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    print_lcd_api((uint8_t*)"Device Name:");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    snprintf(helper_string, 100, "%s", device_name);
    print_lcd_api((uint8_t*)helper_string);
    vTaskDelay(3000 / portTICK_PERIOD_MS);

    print_lcd_api((uint8_t*)"FW Version:");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    snprintf(helper_string, 100, "%hu", fota_get_fw_version());
    print_lcd_api((uint8_t*)helper_string);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
}

void file_core_print_details() {
    uint64_t device_id;
    int      ret_deviceid = file_core_get(NVS_DEVICE_ID, &device_id);

    char ssid_name[MAX_SSID_LEN];
    int  ret_ssid_name = file_core_get(NVS_SSID_NAME, ssid_name);

    char ssid_pw[MAX_PW_LEN];
    int  ret_ssid_pw = file_core_get(NVS_SSID_PW, ssid_pw);

    char ip[MAX_IP_LEN];
    int  ret_ip = file_core_get(NVS_IP, ip);

    uint16_t port;
    int      ret_port = file_core_get(NVS_PORT, &port);

    char device_name[MAX_DEVICE_NAME];
    int  ret_device_name = file_core_get(NVS_DEVICE_NAME, device_name);

    if (ret_deviceid == ITEM_GOOD) {
        ESP_LOGI(TAG, "device id %u", (uint32_t)device_id);
    } else {
        ESP_LOGE(TAG, "device id not set!");
    }

    if (ret_ssid_name == ITEM_GOOD) {
        ESP_LOGI(TAG, "ssid name == %s", ssid_name);
    } else {
        ESP_LOGE(TAG, "ssid name not set!");
    }

    if (ret_ssid_pw == ITEM_GOOD) {
        ESP_LOGI(TAG, "pw == %s", ssid_pw);
    } else {
        ESP_LOGE(TAG, "pw not set!");
    }

    if (ret_ip == ITEM_GOOD) {
        ESP_LOGI(TAG, "ip == %s", ip);
    } else {
        ESP_LOGE(TAG, "IP not set!");
    }

    if (ret_port == ITEM_GOOD) {
        ESP_LOGI(TAG, "port == %hu", port);
    } else {
        ESP_LOGE(TAG, "port not set!");
    }

    if (ret_device_name == ITEM_GOOD) {
        ESP_LOGI(TAG, "device_name == %s", device_name);
    } else {
        ESP_LOGE(TAG, "device name not set!");
    }
}

void file_core_clear_journal() {
    uint16_t dont_matter = 0;
    file_core_set(NVS_JOURNAL_VALID, &dont_matter);
}

void file_core_set_journal(uint16_t uid) {
    file_core_set(NVS_JOURNAL, &uid);

    uint16_t dont_matter = 666;
    file_core_set(NVS_JOURNAL_VALID, &dont_matter); // any non zero value works
}

bool file_core_get_journal(uint16_t* uid) {
    if (!uid) {
        ESP_LOGE(TAG, "UID == NULL");
        ASSERT(0);
    }

    uint16_t test = 0;
    file_core_get(NVS_JOURNAL_VALID, &test);
    if (test == 0) {
        return false;
    }

    file_core_get(NVS_JOURNAL, uid);
    return true;
}
