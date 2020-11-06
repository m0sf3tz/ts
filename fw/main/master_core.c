#include "assert.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>
#include <sys/param.h>

#include "file_core.h"
#include "fota_task.h"
#include "lcd.h"
#include "ll.h"
#include "master_core.h"
#include "packet.h"
#include "parallax.h"
#include "qcore.h"
#include "sync_task.h"
#include "system_defines.h"
#include "tcp_core.h"

/**********************************************************
*              MASTER CORE GLOBAL VARIABLES
**********************************************************/
#ifdef TEST_MODE
bool disable_next_ack;
#endif
QueueHandle_t master_to_fota_q;
QueueHandle_t master_to_suicide_q;

/**********************************************************
*              MASTER CORE STATIC VARIABLES
**********************************************************/
static QueueSetHandle_t        master_core_events;
static const char              TAG[] = "MASTER_CORE";
static SemaphoreHandle_t       master_core_protected;
static SemaphoreHandle_t       master_core_processing_cmd;
static uint8_t                 packets_in_flight;
static uint8_t                 generic_pkt[PACKET_LEN_MAX];            // holder for data when the RX_LL is popped
static uint8_t                 multi_part_generic_pkt[PACKET_LEN_MAX]; // holder for data when the RX_LL is popped
static uint8_t                 oustanding_commands;
static uint8_t                 multi_part_spare;
static master_core_reg_state_e master_core_status;
static bool                    fota_underway;
static bool                    sync_underway;

/**********************************************************
*                  FORWARD DECLERATIONS
**********************************************************/
bool delete_specific_user(bool sync, bool remove_print, uint16_t iid);

/**********************************************************
*                  MASTER CORE FUCTIONS
**********************************************************/

static uint64_t get_device_id() {
    int      status;
    uint64_t deviceId;

    status = file_core_get(NVS_DEVICE_ID, &deviceId);
    if (ITEM_GOOD != status) {
        ESP_LOGE(TAG, "Failed to fetch deviceId from memory. error = %d", status);
        return MANUFACTURING_DEVICE_ID;
    }
    return deviceId;
}

bool get_device_name(char* name) {
    int status;

    if (!name) {
        ASSERT(0);
    }

    status = file_core_get(NVS_DEVICE_NAME, name);
    if (ITEM_GOOD != status) {
        ESP_LOGE(TAG, "Failed to device name from memory. error = %d", status);
        return false;
    } else {
        return true;
    }
}

static bool get_master_core_outstanding_commands() {
    BaseType_t xStatus = xSemaphoreTake(master_core_processing_cmd, 0);
    if (xStatus != pdTRUE) {
        ESP_LOGE(TAG, "Failed to get master core busy mutex");
        return true;
    }
    ESP_LOGE(TAG, "Got master core busy mutex");
    return false;
}

static void give_master_core_outstanding_commands() {
    xSemaphoreGive(master_core_processing_cmd);
}

static master_core_reg_state_e get_master_core_status() {
    master_core_reg_state_e ret;
    BaseType_t xStatus = xSemaphoreTake(master_core_protected, MASTER_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }

    ret = master_core_status;
    xSemaphoreGive(master_core_protected);
    return ret;
}

static void set_master_core_status(master_core_reg_state_e stat) {
    BaseType_t xStatus = xSemaphoreTake(master_core_protected, MASTER_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }

    master_core_status = stat;
    xSemaphoreGive(master_core_protected);
}

void send_to_tcp_core(void* packet) {
    BaseType_t xStatus = xQueueSendToBack(tcp_core_send, packet, MASTER_TIMEOUT);
    if (xStatus != pdPASS) {
        ESP_LOGI(TAG, "Could not write to tcp_core_send! giving up!");
        ASSERT(0);
    }
}
// transaction ID's that are created in the DEVICE range from [0    - 1000)
// transaction ID's that are created in the SERVER range from [2000 - 3000)
uint16_t create_transaction_id() {
    static uint16_t id; //holds the next valid ID
    uint16_t        ret;

    BaseType_t xStatus = xSemaphoreTake(master_core_protected, MASTER_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }
    id++;
    id  = id % 1000;
    ret = id;
    xSemaphoreGive(master_core_protected);

    return ret;
}

// used to delay attach to server
void delay_boot(){
  uint32_t delay = esp_random();
  delay = (delay & 0x2F) + 15;
  if (delay > 60){
    delay = 60; 
  }
  ESP_LOGI(TAG, "Sleeping for %d seconds", delay);
  vTaskDelay(1000 * (delay/ portTICK_PERIOD_MS));
}

void set_fota_underway(bool underway) {
    BaseType_t xStatus = xSemaphoreTake(master_core_protected, MASTER_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }

    fota_underway = underway;
    xSemaphoreGive(master_core_protected);
}

bool get_fota_underway() {
    BaseType_t xStatus = xSemaphoreTake(master_core_protected, MASTER_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }
    bool ret = fota_underway;
    xSemaphoreGive(master_core_protected);
    return ret;
}

static void fota_handler() {
    fota_pkt_payload_t fota_payload;
    BaseType_t         xStatus;
    uint16_t           ti = create_transaction_id();

    //unpack
    packet_fota_unpack(generic_pkt, &fota_payload);
    if (get_fota_underway()) {
        if (fota_payload.type == FOTA_META_PACKET || fota_payload.type == FOTA_FINAL_PACKET || fota_payload.type == FOTA_FINAL_TEST_ONLY) {
            // shortcircuit to fota task
            ESP_LOGI(TAG, "rxed a meta/final/final test packet, fwding to fota core");
            xStatus = xQueueSendToBack(master_to_fota_q, generic_pkt, MASTER_TO_FOTA_Q_TIME_OUT);
            if (xStatus != pdPASS) {
                ESP_LOGE(TAG, "Timed out sending data packet to fota task");
                ASSERT(0);
            }
            return;
        } else {
            ESP_LOGE(TAG, "Attempted to start FOTA while FOTA underway, type == %hu", fota_payload.type);
            ASSERT(0);
        }
    }

    // Make sure the FW is new
    if (fota_payload.fw_version == fota_get_fw_version()) {
        ESP_LOGE(TAG, "Requested to FOTA to same FW version, won't do it!");
        packet_fota_rsp_create(generic_pkt,               // Backing array
                               ti,                        // Transaction ID
                               FOTA_START_ACK,            // Response type
                               FOTA_STATUS_FAILED_SAME_FW // Status
        );

        ESP_LOGE(TAG, "%d here", packet_get_transaction_id(generic_pkt));
        goto send_packet;
    }

    xStatus = xTaskCreate(fota_task,            // function
                          "FOTA task",          // name
                          8192,                 // stack size
                          NULL,                 // no parameter
                          MASTER_CORE_PRIORITY, // priority
                          NULL);                // handle
    if (xStatus != pdPASS) {
        ESP_LOGI(TAG, "Could not create FOTA thread, giving up");
        ASSERT(0);
    }

    // relay initial fota packet to fota_task
    xStatus = xQueueSendToBack(master_to_fota_q, generic_pkt, MASTER_TO_FOTA_Q_TIME_OUT);
    if (xStatus != pdPASS) {
        ESP_LOGI(TAG, "Could not send fota information to fota_task");
        ASSERT(0);
    }

    packet_fota_rsp_create(generic_pkt,     // Backing array
                           ti,              // Transaction ID
                           FOTA_START_ACK,  // Response type
                           FOTA_STATUS_GOOD // Status
    );

send_packet:
    ll_add_node(CR_LL,
                generic_pkt,
                FOTA_ACK_PACKET_SIZE,
                ti,
                DONT_STORE_DATA);

    send_to_tcp_core(generic_pkt);
}

static void reset_task(void* v) {
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    esp_restart();
}

/* will reset after a while */
static void suicide_timer(void* v) {
    int counter = TIME_TO_SUICIDE;
    int dont_care;
    while (counter--) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "Tick... %d cycles left before suicide", counter);
        BaseType_t xStatus = xQueueReceive(master_to_suicide_q, &dont_care, DONT_WAIT_QUEUE);
        if (xStatus != pdPASS) {
            continue;
        } else {
            ESP_LOGI(TAG, "Suicide task diffused, enabling IRQs");
            /* enable IRQs so people can log in again */
            set_irq_state(ENABLE_IRQS);
            vTaskDelete(NULL);
        }
    }
    esp_restart();
}

static void ack_stress_test(void* v) {
    ESP_LOGI(TAG, "Starting acK_stress_test, server side");

    uint16_t ti = create_transaction_id();
    packet_cmd_resp_create(multi_part_generic_pkt,                 // reuse this buffer
                           ti,                                     // new transaction ID
                           packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                           1,                                      // total_packets
                           0,                                      // no packets remaning
                           CMD_STATUS_GOOD,                        // cmd_status
                           0,                                      // sizeof payload
                           NULL                                    // response payload
    );

    int counter = 1500;
    /* test STARTs*/
    while (counter--) {
        ESP_LOGI(TAG, "Sent one ACK!");

        ti = create_transaction_id();
        packet_void_create(multi_part_generic_pkt, ti);

        ll_add_node(CR_LL,
                    &multi_part_generic_pkt,
                    VOID_PACKET_SIZE,
                    ti,
                    STORE_DATA);

        xQueueSendToBack(tcp_core_send, multi_part_generic_pkt, portMAX_DELAY);
        vTaskDelay(5);
    }
    /* test ENSD */

    ESP_LOGI(TAG, "Sending ACK");
    ti = create_transaction_id();
    packet_cmd_resp_create(multi_part_generic_pkt,            // reuse this buffer
                           ti,                                // new transaction ID
                           0, /*this is WRONG, should be TI*/ // transaction_id of orig cmd
                           1,                                 // total_packets
                           0,                                 // which reponse packet
                           CMD_STATUS_GOOD,                   // cmd_status
                           0,                                 // sizeof payload
                           NULL                               // response payload
    );

    ll_add_node(CR_LL,
                &multi_part_generic_pkt,
                CMD_RESP_PACKET_SIZE,
                ti,
                STORE_DATA);

    xQueueSendToBack(tcp_core_send, multi_part_generic_pkt, portMAX_DELAY);

    ESP_LOGI(TAG, "Done acK_stress_test, server side");
    vTaskDelete(NULL);
}

/* this function also syncs, if it is requested */
static void send_back_all_users(void* sync) {
    file_core_mutex_take();

    /* should probably take a mutex for this */
    ESP_LOGI(TAG, "Server requested all users, sending back all %d users", file_core_total_users);

    int i, total_packets_to_send;
    i                     = 0;
    total_packets_to_send = file_core_total_users;
    uint16_t      ti;
    employee_id_t employee_s;

    if (!file_core_total_users) {
        ESP_LOGI(TAG, "No users registered...");

        ti = create_transaction_id();
        packet_cmd_resp_create(multi_part_generic_pkt,                 // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               1,                                      // total_packets
                               0,                                      // which reponse packet
                               FILE_MEM_EMPTY,                         // cmd_status
                               0,                                      // sizeof payload
                               NULL                                    // response payload
        );

        ll_add_node(CR_LL,
                    &multi_part_generic_pkt,
                    CMD_RESP_PACKET_SIZE,
                    ti,
                    DONT_STORE_DATA);

        send_to_tcp_core(multi_part_generic_pkt);
        file_core_mutex_give();

        give_master_core_outstanding_commands();
        vTaskDelete(NULL);
    }

    for (i = 0; i != MAX_EMPLOYEE; i++) {
        if (false == file_core_all_users_arr_valid[i]) {
            continue;
        }
        employee_s.id  = i;
        employee_s.uid = file_core_all_users_arr[i].uid;
        memcpy(employee_s.name, file_core_all_users_arr[i].name, MAX_NAME_LEN_PLUS_NULL);

        ESP_LOGI(TAG, "Sending back uuid=%u, internal_id = %hu", employee_s.uid, employee_s.id);

        /* Payload will be number of responses left to send */
        ti = create_transaction_id();
        packet_cmd_resp_create(multi_part_generic_pkt,                 // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               total_packets_to_send,                  // total_packets
                               i,                                      // which reponse packet
                               CMD_STATUS_GOOD,                        // cmd_status
                               strlen(file_core_all_users_arr[i].name) + sizeof(employee_s.id) + sizeof(employee_s.uid),
                               &employee_s // response payload
        );

        ll_add_node(CR_LL,
                    &multi_part_generic_pkt,
                    CMD_RESP_PACKET_SIZE,
                    ti,
                    DONT_STORE_DATA);

        send_to_tcp_core(multi_part_generic_pkt);
    }

    file_core_mutex_give();

    give_master_core_outstanding_commands();
    vTaskDelete(NULL);
}

static void handle_multi_part_response_test(void* packets_to_send) {
    int8_t* parts_to_send = (cmd_payload_t*)packet_cmd_get_payload_data(generic_pkt);
    // for a multi-part test packet, the first byte of the payload is how many responses to send
    ESP_LOGI(TAG, "Sending back %d as part of a multi-part test-command", *(uint8_t*)packets_to_send);

    int i, total_packets_to_send;
    i                     = 0;
    total_packets_to_send = *(uint8_t*)packets_to_send;

    for (; i != total_packets_to_send; i++) {
        /* Payload will be number of responses left to send */
        uint16_t ti = create_transaction_id();
        packet_cmd_resp_create(multi_part_generic_pkt,                 // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               total_packets_to_send,                  // total_packets
                               i,                                      // which reponse packet
                               CMD_STATUS_GOOD,                        // cmd_status
                               1,                                      // sizeof payload (1 byte)
                               &i                                      // response payload
        );

        ll_add_node(CR_LL,
                    &multi_part_generic_pkt,
                    CMD_RESP_PACKET_SIZE,
                    ti,
                    DONT_STORE_DATA);

        send_to_tcp_core(multi_part_generic_pkt);
    }

    give_master_core_outstanding_commands();
    vTaskDelete(NULL);
}

/* Will say it will send back 10 packets, but only send back 5 */
static void handle_multi_part_missing_response_test(void* packets_to_send) {
    ESP_LOGI(TAG, "Sending back 5 packets, but saying that we will send back 10, expect to fail");

    int i, total_packets_to_send;
    i                     = 0;
    total_packets_to_send = 10;

    for (; i != 5; i++) {
        /* Payload will be number of responses left to send */
        uint16_t ti = create_transaction_id();
        packet_cmd_resp_create(multi_part_generic_pkt,                 // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               total_packets_to_send,                  // total_packets
                               i,                                      // which reponse packet
                               CMD_STATUS_GOOD,                        // cmd_status
                               1,                                      // sizeof payload (1 byte)
                               &i                                      // response payload
        );

        ll_add_node(CR_LL,
                    &multi_part_generic_pkt,
                    CMD_RESP_PACKET_SIZE,
                    ti,
                    DONT_STORE_DATA);

        send_to_tcp_core(multi_part_generic_pkt);
    }

    give_master_core_outstanding_commands();
    vTaskDelete(NULL);
}

static void master_core_create_lcd_message(char* name_buf, bool signInOrSignOut) {
    if (signInOrSignOut) {
        print_lcd_api((void*)"Welcome:");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        print_lcd_api((void*)name_buf);
    } else {
        print_lcd_api((void*)"Goodbye:");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        print_lcd_api((void*)name_buf);
    }
}

/* missnomer, is also logout */
static void master_core_handle_login() {
    static char      name_buf[MAX_NAME_LEN_PLUS_NULL];
    parallax_login_t login;
    uint8_t bricked = file_core_is_bricked();

    ESP_LOGI(TAG, "Handling new login/logout");

    BaseType_t xStatus = xQueueReceive(parallaxLoginQ, &login, 0);
    if (xStatus != pdPASS) {
        ASSERT(0);
    }

    if (login.id == PARALLAX_ERROR_USER_ID) {
        ESP_LOGE(TAG, "parallax could not determine who just tried to login/logout");
        ASSERT(0);
    }

    ESP_LOGI("TAG", "User %d login/logout==%hhu", login.id, (uint8_t)login.signIn);

    if (TCP_CORE_DOWN == get_tcp_core_status()) {
        print_lcd_api((void*)"Failed - no connection");
        return;
    }

    // Test if TCP core is up, if not, put the login info into flash till the core is up
    if (MASTER_CORE_NOT_REGISTERED == get_master_core_status()) {
        print_lcd_api((void*)"Failed - no connection");
        return;
    }

    if(bricked){
        print_lcd_api((void*)"Device Bricked... Login failed");
        return;
    }

    uint32_t uid;
    memset(name_buf, 0, MAX_NAME_LEN_PLUS_NULL);
    if (file_core_get_username(name_buf, login.id, &uid) != FILE_RET_OK) {
        ESP_LOGE(TAG, "Unknown user logged in - bricking device :(");
      
        uint8_t brick_val = UNKNOWN_LOGIN_BRICKED;
        file_core_set(NVS_BRICKED, &brick_val);
        esp_restart();
        return;
    } else {
        master_core_create_lcd_message(name_buf, login.signIn);
        ESP_LOGI(TAG, "User %s logged in", name_buf);
    }
    uint16_t    transaction_id = create_transaction_id();
    login_pkt_t login_pkt;

    packet_login_create(&login_pkt,     // Packet
                        name_buf,       // name of person who just signed in
                        0,              // Temperature of the new sign in /*deprecated*/
                        transaction_id, // Get a brand new tranaction ID for this packet
                        login.signIn,   // Login or logout
                        uid             // user UID
    );

    // Add this packet to the CR
    //  LL so we can keep track
    //  of it
    ll_add_node(CR_LL,             // add to the core ll
                &login_pkt,        // the packet to add to the ll
                LOGIN_PACKET_SIZE, // the size of the packet
                transaction_id,    // tranasction_id of the new packet
                STORE_DATA         // store the data in the linked-list incase we need it later
    );

    // send to the Qcore
    xStatus = xQueueSendToBack(tcp_core_send, &login_pkt, MASTER_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }
}

//packet to be processed is in the static global variable general_pkt
static void process_echo_pkt() {
    ESP_LOGE(TAG, "Got an echo packet");
    echo_pkt_t* echo_p     = &generic_pkt;
    echo_p->transaction_id = echo_p->transaction_id - 2000; // 2000 being offset between DEVICE and SERVER id's

    ll_add_node(CR_LL,
                &generic_pkt,
                CMD_RESP_PACKET_SIZE,
                echo_p->transaction_id,
                STORE_DATA);

    BaseType_t xStatus = xQueueSendToBack(tcp_core_send, generic_pkt, MASTER_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }
}

void add_user(bool add_print) {
    uint16_t            slot_id;
    commandQ_file_t     file_cmd;
    commandQ_parallax_t parallax_cmd;
    uint8_t             response = CMD_STATUS_FAILED;
    uint16_t            ti       = 0;

    /* Parse the details (id, username) and put it in flash */
    add_user_payload_t* user_details = packet_add_user_parse(generic_pkt);

    // If a user exists, but command is asking us to replace, we will replace the user (IE, get a new print taken for them)
    uint16_t id; // this is the "slot" the user is held in filecore+thumb, not to be confused with UID
    if (file_core_user_exists(user_details->uid, &id)) {
        if (user_details->replace) {
            ESP_LOGW(TAG, "User exists, but site is asking us to replace... Will delete FIRST!");
            
            file_core_set_journal(id);
        
            delete_specific_user(true,         // "sync == true, this way, we won't send spruious responses back to the server'
                                 add_print,    // remove thumb + file
                                 id);
        
            file_core_clear_journal();
     
            // reuse slot-id  
            slot_id = id;

        } else {
            uint16_t ti = create_transaction_id();
            ESP_LOGE(TAG, "User exists, but site did not ask for replace");
            packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                                   ti,                                     // new transaction ID
                                   packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                                   1,                                      // total_packets
                                   0,                                      // no packets remaning
                                   CMD_STATUS_UID_EXISTS,                  // cmd_status
                                   0,                                      // sizeof payload
                                   NULL                                    // response payload
            );

            ll_add_node(CR_LL,
                        &generic_pkt,
                        CMD_RESP_PACKET_SIZE,
                        ti,
                        STORE_DATA);

            ESP_LOGW(TAG, "Failed to get new ID, failed with response: %d", response);
            goto send_packet;
        }
    } else {
      // Get a new user ID
      ESP_LOGW(TAG, "New user, will get a new ID to use!");

      file_cmd.command      = FILE_GET_FREE_ID;
      file_cmd.next_free_id = &slot_id;
      response              = file_thread_gate(file_cmd);

      if (response != FILE_RET_OK) {
          uint16_t ti = create_transaction_id();
          packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                                 ti,                                     // new transaction ID
                                 packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                                 1,                                      // total_packets
                                 0,                                      // no packets remaning
                                 response,                               // cmd_status
                                 0,                                      // sizeof payload
                                 NULL                                    // response payload
          );

          ll_add_node(CR_LL,
                      &generic_pkt,
                      CMD_RESP_PACKET_SIZE,
                      ti,
                      STORE_DATA);

          ESP_LOGW(TAG, "Failed to get new ID, failed with response: %d", response);
          goto send_packet;
        }

        ESP_LOGI(TAG, "Adding new user w/ ID : %hu", slot_id);
        if (slot_id < 0 || slot_id > MAX_EMPLOYEE) {
            ESP_LOGE(TAG, "Got an unnexpected user ID %hu", slot_id);
            ASSERT(0);
        }
      }

     /* add to journal that we are adding a new user */
     file_core_set_journal(slot_id);

    if (add_print) {
        // Add our new user to print
        parallax_cmd.command = PARALLAX_ADD_USER;
        parallax_cmd.id      = slot_id;
        response             = parallax_thread_gate(&parallax_cmd);
        if (response != ADDED_USER) {
            ti = create_transaction_id();
            packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                                   ti,                                     // new transaction ID
                                   packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                                   1,                                      // total_packets
                                   0,                                      // no packets remaning
                                   response,                               // cmd_status
                                   0,                                      // sizeof payload
                                   NULL                                    // response payload
            );

            ll_add_node(CR_LL,
                        &generic_pkt,
                        CMD_RESP_PACKET_SIZE,
                        ti,
                        STORE_DATA);

            ESP_LOGW(TAG, "Failed to add user, failed with response: %d", response);
            goto send_packet;
        }
    }

    file_cmd.command = FILE_ADD_USER;
    file_cmd.name    = user_details->name;
    file_cmd.uid     = user_details->uid;
    file_cmd.id      = slot_id;
    response         = file_thread_gate(file_cmd);

    if (FILE_RET_OK != response) {
        uint16_t ti = create_transaction_id();
        packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               1,                                      // total_packets
                               0,                                      // no packets remaning
                               CMD_STATUS_FAILED,                      // cmd_status
                               0,                                      // sizeof payload
                               NULL                                    // response payload
        );

        ll_add_node(CR_LL,
                    &generic_pkt,
                    CMD_RESP_PACKET_SIZE,
                    ti,
                    STORE_DATA);
        /* this is bad, we added a user to parallax, but not flash, we will assert here so the journaling will erase the user added to parallax */
        esp_restart();
    } else {
        ESP_LOGI(TAG, "No issues commiting user to memory");
        uint16_t ti = create_transaction_id();
        packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               1,                                      // total_packets
                               0,                                      // no packets remaning
                               CMD_STATUS_GOOD,                        // cmd_status
                               0,                                      // sizeof payload
                               NULL                                    // response payload
        );

        ll_add_node(CR_LL,
                    &generic_pkt,
                    CMD_RESP_PACKET_SIZE,
                    ti,
                    STORE_DATA);
    }

send_packet:
    /* erease journal */
    file_core_clear_journal();
    BaseType_t xStatus = xQueueSendToBack(tcp_core_send, generic_pkt, MASTER_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }
}

bool delete_specific_user(bool sync, bool remove_print, uint16_t iid) {
    int                 response;
    uint16_t            ti;
    commandQ_parallax_t parallax_cmd;
    commandQ_file_t     file_cmd;
    uint16_t            internal_id;
    BaseType_t          xStatus;

    if (!sync) {
        internal_id = *(uint16_t*)packet_cmd_get_payload_data(generic_pkt);
    } else {
      internal_id = iid;
    }

    ESP_LOGI(TAG, " Reqesetd to delete user %d", internal_id);

    if (internal_id >= MAX_EMPLOYEE) {
        ESP_LOGE(TAG, " Reqesetd to delete user %d, this is too large!", internal_id);
        ASSERT(0);
    }
    
    if (!sync) {
      if (false == file_core_all_users_arr_valid[internal_id]) {
            ti = create_transaction_id();
            ESP_LOGE(TAG, "Requested to delete a user that does not exist!");
            packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                                   ti,                                     // new transaction ID
                                   packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                                   1,                                      // total_packets
                                   0,                                      // no packets remaning
                                   FILE_RET_USER_NOT_EXIST,                // cmd_status
                                   0,                                      // sizeof payload
                                   NULL                                    // response payload
            );

            ll_add_node(CR_LL,
                        &generic_pkt,
                        CMD_RESP_PACKET_SIZE,
                        ti,
                        STORE_DATA);

            xStatus = xQueueSendToBack(tcp_core_send, generic_pkt, MASTER_TIMEOUT);
            if (xStatus != pdTRUE) {
                ASSERT(0);
            }
            return false;
        }
    }

    file_cmd.command = FILE_DELETE_USER;
    file_cmd.id      = internal_id;
    response         = file_thread_gate(file_cmd);

    if(response != FILE_RET_OK){
      // yikes, terrible, don't try to delete from Parallax so we maintaine state across flash+thumb
      ESP_LOGE(TAG, "Could not delete user!");
      return false; 
    }
    
    ESP_LOGI(TAG, "response of operation (delete flash)== %hu", response);

    int retry_counter = 0;
    do {
        if (!remove_print) {
            break;
        }

        parallax_cmd.command = PARALLAX_DLT_SPECIFIC;
        parallax_cmd.id      = internal_id;
        response             = parallax_thread_gate(&parallax_cmd);

        retry_counter++;
        if (retry_counter == 3) {
            //not good, we will brick the device
            uint8_t brick_val = FAILED_TO_DELETE_THUMB_BRICKED;

            file_core_set(NVS_BRICKED, &brick_val);
            esp_restart();
        }

        //confusing, but this resets parallax
        reset_device();

    } while (response);

    if (remove_print) {
        ESP_LOGI(TAG, "response of operation (delete print)== %hu", response);
    }

    if (!sync) {
      ti = create_transaction_id();
      packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               1,                                      // total_packets
                               0,                                      // no packets remaning
                               response,                               // cmd_status
                               0,                                      // sizeof payload
                               NULL                                    // response payload
        );

        ll_add_node(CR_LL,
                    &generic_pkt,
                    CMD_RESP_PACKET_SIZE,
                    ti,
                    STORE_DATA);

        xStatus = xQueueSendToBack(tcp_core_send, generic_pkt, MASTER_TIMEOUT);
        if (xStatus != pdTRUE) {
            ASSERT(0);
        }
    }
    return true;
}

bool delete_specific_user_wrapper(bool sync, bool remove_print, uint16_t iid) {
    file_core_set_journal(iid);
    bool ret = delete_specific_user(sync, remove_print, iid);
    file_core_clear_journal();
    return ret;
}

static void display_message(){
  uint16_t ti = create_transaction_id();
  uint8_t msg = *(uint8_t*)packet_cmd_get_payload_data(generic_pkt);

  packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                         ti,                                     // new transaction ID
                         packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                         1,                                      // total_packets
                         0,                                      // no packets remaning
                         CMD_STATUS_GOOD,                        // cmd_status
                         0,                                      // sizeof payload
                         NULL                                    // response payload
  );

  ll_add_node(CR_LL,
              &generic_pkt,
              CMD_RESP_PACKET_SIZE,
              ti,
              STORE_DATA);

  
  //add a bit of a delay so the user gets some feedback
  vTaskDelay(1500 / portTICK_PERIOD_MS);

  print_lcd_api((void*)"Within Cooldown Ignored - Normal!");

  vTaskDelay(2500 / portTICK_PERIOD_MS);
  print_lcd_api((void*)"Try Again In 2  Minutes. Thanks!");
  
  BaseType_t xStatus = xQueueSendToBack(tcp_core_send, generic_pkt, MASTER_TIMEOUT);
  if (xStatus != pdTRUE) {
      ASSERT(0);
  }
}

bool sync_cmd_process() {
    sync_pkt_payload_t sync_payload;
    packet_sync_unpack(packet_cmd_get_payload(generic_pkt), &sync_payload);

    uint32_t mode = sync_payload.mode;
    ESP_LOGI(TAG, "Sync mode with mode == %hhu", mode);

    uint32_t crc_cal = crc32(sync_payload.valid_bit_field, MAX_EMPLOYEE);
    ESP_LOGI(TAG, "CRC32 for sync packet == %u, calculated = %u", sync_payload.crc_32, crc_cal);

    if (crc_cal != sync_payload.crc_32) {
        ESP_LOGE(TAG, "CRC MISSPATCH!");
        return CMD_STATUS_FAILED_CRC;
    }

    int  i;
    bool ret = CMD_STATUS_GOOD;
    for (i = 0; i < MAX_EMPLOYEE; i++) {
        //stupid hack so I don't have to update any codee...
        print_lcd_api((void*)"Busy Syncing!..");

        if (sync_payload.valid_bit_field[i] == SYNC_DELETE_USER_BIT_FIELD) {
            if (mode == SYNC_TEST_MODE) {
                ret |= delete_specific_user_wrapper(true, DEL_USER_FLASH, i);
            } else {
                ret |= delete_specific_user_wrapper(true, DEL_USER_FLASH_PLUS_PRINT, i);
            }
        }
    }
    return ret;
}

//packet to be processed is in the static global variable general_pkt
static void process_cmd_pkt() {
    uint8_t             cmd_type = packet_cmd_get_type(generic_pkt);
    commandQ_file_t     file_cmd;
    commandQ_parallax_t parallax_cmd;
    uint8_t             response = CMD_STATUS_FAILED;
    uint16_t            ti       = 0;
    file_cmd.id                  = 0;
    uint8_t  free_id;
    uint16_t internal_id;
    BaseType_t xStatus;

    /*chain command related stuff */
    static bool    chained_command;
    static uint8_t next_chained_command;

    /* If there is already an outstanding command, send back a fail */
    if (get_master_core_outstanding_commands()) {
        uint16_t ti = create_transaction_id();
        packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               1,                                      // total_packets
                               0,                                      // no packets remaning
                               CMD_STATUS_FAILED,                      // cmd_status
                               0,                                      // sizeof payload
                               NULL                                    // response payload
        );
        ESP_LOGW(TAG, "Failed to process new command as master core is busy");
        xStatus = xQueueSendToBack(tcp_core_send, generic_pkt, MASTER_TIMEOUT);
        if (xStatus != pdTRUE) {
            ASSERT(0);
        }
    }

    /*check for chained commansd*/
    if (chained_command) {
        if (cmd_type != next_chained_command) {
            ESP_LOGE(TAG, "Chained sequence broken, expected %hhu, got %hhu", next_chained_command, cmd_type);
            ASSERT(0);
        } else {
            chained_command = false;
        }
    }

    switch (cmd_type) {
    case ADD_USER_CMD:
        if (!packet_cmd_get_payload_data(generic_pkt)) {
            ESP_LOGE(TAG, "no payload! in process_cmd_pkt when adding a user!");
            ASSERT(0);
        }
        add_user(ADD_USER_FLASH_PLUS_PRINT);

        give_master_core_outstanding_commands();
        break;
    case DELETE_ALL_USERS_CMD:
        ESP_LOGI(TAG, "Got a delete ALL users cmd");
        commandQ_parallax_t parallax_cmd;
        parallax_cmd.command = PARALLAX_DLT_ALL;
        parallax_thread_gate(&parallax_cmd);

        commandQ_file_t file_cmd;
        file_cmd.command = FILE_DELETE_ALL_USERS;
        file_thread_gate(file_cmd);

        ti = create_transaction_id();
        packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               1,                                      // total_packets
                               0,                                      // no packets remaning
                               CMD_STATUS_GOOD,                        // cmd_status
                               20,                                     // sizeof payload
                               NULL                                    // response payload
        );

        ll_add_node(CR_LL,
                    &generic_pkt,
                    CMD_RESP_PACKET_SIZE,
                    ti,
                    STORE_DATA);

        xStatus = xQueueSendToBack(tcp_core_send, generic_pkt, MASTER_TIMEOUT);
        if (xStatus != pdTRUE) {
            ASSERT(0);
        }
        
        give_master_core_outstanding_commands();
        break;
    case GET_ALL_USERS_CMD:
        ESP_LOGI(TAG, "Got a request to send back all existing users");

        BaseType_t xStatus = xTaskCreate(send_back_all_users,   // function
                                         "send back all users", // name
                                         2048,                  // stack size
                                         NULL,                  // how many packets to send
                                         MASTER_CORE_PRIORITY,  // priority
                                         NULL);                 // handle
        if (xStatus != pdPASS) {
            ESP_LOGI(TAG, "Could not create send_back_all_users .. Giving up and restarting");
            esp_restart();
        }
        /* don't need to give back mutex, the send_back_all_users thread will do it */
        break;
    case GET_ALL_USERS_AND_SYNC_CMD:
        ESP_LOGI(TAG, "Got a sync-start command");
        //needs to be static as it's being passed to a new thread and stack might be clobbered
        static bool sync; 
        sync = true;

        xStatus = xTaskCreate(send_back_all_users,   // function
                              "send back all users", // name
                              2048,                  // stack size
                              &sync,                 // sync or not to sync
                              MASTER_CORE_PRIORITY,  // priority
                              NULL);                 // handle
        if (xStatus != pdPASS) {
            ESP_LOGI(TAG, "Could not create send_back_all_users (sync).. Giving up and restarting");
            esp_restart();
        }

        // Disable IRQs so peopel can't log in while we send back infromation
        set_irq_state(DISABLE_IRQS);

        /* start the suicide taks */
        xStatus = xTaskCreate(suicide_timer,        // function
                              "Suicide task",       // name
                              2048,                 // stack size
                              0,                    // sync or not to sync
                              MASTER_CORE_PRIORITY, // priority
                              NULL);                // handle
        if (xStatus != pdPASS) {
            ESP_LOGI(TAG, "Could not create suicide task (sync).. Giving up and restarting");
            esp_restart();
        }

        /* sets up the chained sequnce */
        chained_command      = true;
        next_chained_command = SYNC_COMMAND;

        /* don't need to give back mutex, the send_back_all_users thread will do it */
        break;
    case SYNC_COMMAND:
        ESP_LOGI(TAG, "Got a sync-end command");
        int status = sync_cmd_process();
        ti         = create_transaction_id();

        packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               1,                                      // total_packets
                               0,                                      // no packets remaning
                               status,                                 // cmd_status
                               0,                                      // sizeof payload
                               NULL                                    // response payload
        );

        ll_add_node(CR_LL,
                    &generic_pkt,
                    CMD_RESP_PACKET_SIZE,
                    ti,
                    STORE_DATA);

        int dont_care;
        xStatus = xQueueSendToBack(master_to_suicide_q, &dont_care, MASTER_TIMEOUT);
        if (xStatus != pdTRUE) {
            ASSERT(0);
        }

        xStatus = xQueueSendToBack(tcp_core_send, generic_pkt, MASTER_TIMEOUT);
        if (xStatus != pdTRUE) {
          ASSERT(0);
         }
        
        give_master_core_outstanding_commands();
        break;
    case DELETE_SPECIFIC_USER_CMD:
        ESP_LOGI(TAG, "Got a request to delete a speciifc user");
        delete_specific_user_wrapper(SYNC_NOT_UNDERWAY, DEL_USER_FLASH_PLUS_PRINT, NULL_INTERNAL_ID);

        give_master_core_outstanding_commands();
        break;
    case DISPLAY_MSG_LCD:
        ESP_LOGI(TAG, "Got a request to deisplay a message on the LCD");
        display_message();
        
        give_master_core_outstanding_commands();
        break;
    case DISCONNECT_CMD:
        ESP_LOGI(TAG, "Got a disconnect command!");
        ti = create_transaction_id();
        packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               1,                                      // total_packets
                               0,                                      // no packets remaning
                               CMD_STATUS_GOOD,                        // cmd_status
                               20,                                     // sizeof payload
                               NULL                                    // response payload
        );

        ll_add_node(CR_LL,
                    &generic_pkt,
                    CMD_RESP_PACKET_SIZE,
                    ti,
                    STORE_DATA);

        xQueueSendToBack(tcp_core_send, generic_pkt, portMAX_DELAY);
        // waiit a bit for the OK to get to the server, then reset!

        xStatus = xTaskCreate(reset_task,           // function
                              "reset_task",         // name
                              2048,                 // stack size
                              &multi_part_spare,    // how many packets to send
                              MASTER_CORE_PRIORITY, // priority
                              NULL);                // handle
        if (xStatus != pdPASS) {
            ESP_LOGI(TAG, "Could not creat reset task in DISSCONECT_CMD");
            esp_restart();
        }

        give_master_core_outstanding_commands();
        break;
        //**********************************************************************************************************************
        //
        //
        //                               BELOW THIS LINE IS ONLY TEST COMMANDS!
        //
        //
        //*********************************************************************************************************************/
    case ECHO_CMD:
        ESP_LOGI(TAG, "got an ECHO command");
        ti = create_transaction_id();
        packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               1,                                      // total_packets
                               0,                                      // no packets remaning
                               CMD_STATUS_GOOD,                        // cmd_status
                               0,                                      // sizeof payload
                               NULL                                    // response payload
        );

        ll_add_node(CR_LL,
                    &generic_pkt,
                    CMD_RESP_PACKET_SIZE,
                    ti,
                    STORE_DATA);

        ESP_LOGW(TAG, "Sending back an ECHO command: %d", response);
        xQueueSendToBack(tcp_core_send, generic_pkt, portMAX_DELAY);

        give_master_core_outstanding_commands();
        break;
    case TIME_OUT_NEXT_PACKET:
        ESP_LOGI(TAG, "got command to force next packet to time out");
        disable_next_ack = true;

        ti = create_transaction_id();
        packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               1,                                      // total_packets
                               0,                                      // no packets remaning
                               CMD_STATUS_GOOD,                        // cmd_status
                               0,                                      // sizeof payload
                               NULL                                    // response payload
        );

        ll_add_node(CR_LL,
                    &generic_pkt,
                    CMD_RESP_PACKET_SIZE,
                    ti,
                    DONT_STORE_DATA);

        xQueueSendToBack(tcp_core_send, generic_pkt, portMAX_DELAY);

        give_master_core_outstanding_commands();
        break;
    case VOID_CMD:
        ESP_LOGI(TAG, "got a void command, not doing anything");

        give_master_core_outstanding_commands();
        break;
    case SEND_MULTI_PART_RSP: /* Test Only */
        /* how many packets to send back is the first byte of the instruction payload */
        multi_part_spare = *(uint8_t*)packet_cmd_get_payload_data(generic_pkt);

        xStatus = xTaskCreate(handle_multi_part_response_test, // function
                              "multi_part_test_task",          // name
                              2048,                            // stack size
                              &multi_part_spare,               // how many packets to send
                              MASTER_CORE_PRIORITY,            // priority
                              NULL);                           // handle
        if (xStatus != pdPASS) {
            ESP_LOGI(TAG, "Could not create SEND_MULTI_PART_RSP.. Giving up and restarting");
            esp_restart();
        }

        /* thread will handle giving back the busy flag */
        break;
    case SEND_MULTI_PART_RSP_FAIL:
        /* Will say it will send back 10, but only send back 5, expect to fail */
        /* how many packets to send back is the first byte of the instruction payload */
        multi_part_spare = *(uint8_t*)packet_cmd_get_payload_data(generic_pkt);

        xStatus = xTaskCreate(handle_multi_part_missing_response_test, // function
                              "multi_part_missing_test_task",          // name
                              2048,                                    // stack size
                              NULL,                                    // how many packets to send
                              MASTER_CORE_PRIORITY,                    // priority
                              NULL);                                   // handle
        if (xStatus != pdPASS) {
            ESP_LOGI(TAG, "Could not create SEND_MULTI_PART_RSP_FAIL.. Giving up and restarting");
            esp_restart();
        }

        /* thread will handle giving back the busy flag */
        break;
    case ADD_USER_TO_FLASH:
        ESP_LOGI(TAG, " *test command* adding a user to flash");
        add_user(ADD_USER_FLASH);

        give_master_core_outstanding_commands();
        break;
    case DELETE_SPECIFIC_USER_CMD_FLASH:
        ESP_LOGI(TAG, " *test command* deleting a user from flash");
        uint16_t slot_id = *(uint16_t*)packet_cmd_get_payload_data(generic_pkt);
        ESP_LOGI(TAG, "deleting a user from flash, slotId == %hu", slot_id);
        delete_specific_user_wrapper(SYNC_NOT_UNDERWAY, DEL_USER_FLASH, slot_id);

        give_master_core_outstanding_commands();
        break;
    case SEND_TEST_LOGIN_PACKET:
        ASSERT(0); // not working
#if 0
        ti = create_transaction_id();
        login_pkt_t login_pkt;
        char        fake_name[] = "fake name";

        packet_login_create(&login_pkt, // Packet
                            fake_name,  // name of person who just signed in
                            0,          // Temperature of the new sign in
                            ti,         // Get a brand new tranaction ID for this packet
                            true,       // Login or logout (sign in)
                            0xdeadbeef  // user UID
        );

        // Add this packet to the CR
        //  LL so we can keep track
        //  of it
        ll_add_node(CR_LL,             // add to the core ll
                    &login_pkt,        // the packet to add to the ll
                    LOGIN_PACKET_SIZE, // the size of the packet
                    ti,                // tranasction_id of the new packet
                    STORE_DATA         // store the data in the linked-list incase we need it later
        );
        // send to the Qcore
#endif
        give_master_core_outstanding_commands();
        break;
    case SET_DEVICE_ID_TEST:
        ti                = create_transaction_id();
        uint64_t deviceId = *(uint64_t*)packet_cmd_get_payload_data(generic_pkt);
        ESP_LOGI(TAG, "Setting deviceId == %" PRIu64, deviceId);
        status = file_core_set(NVS_DEVICE_ID, &deviceId);

        packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               1,                                      // total_packets
                               0,                                      // no packets remaning
                               status,                                 // cmd_status
                               0,                                      // sizeof payload
                               NULL                                    // response payload
        );

        ll_add_node(CR_LL,
                    &generic_pkt,
                    CMD_RESP_PACKET_SIZE,
                    ti,
                    STORE_DATA);

        xQueueSendToBack(tcp_core_send, generic_pkt, portMAX_DELAY);

        give_master_core_outstanding_commands();
        break;
    case GET_DEVICE_ID_TEST:
        ti = create_transaction_id();

        status = file_core_get(NVS_DEVICE_ID, &deviceId);
        if (ITEM_GOOD != status) {
            ESP_LOGE(TAG, "Failed to fetch userID from memory. error = %d", status);
        }

        ESP_LOGI(TAG, "got deviceId == %" PRIu64, deviceId);
        packet_cmd_resp_create(generic_pkt,                            // reuse this buffer
                               ti,                                     // new transaction ID
                               packet_get_transaction_id(generic_pkt), // transaction_id of orig cmd
                               1,                                      // total_packets
                               0,                                      // no packets remaning
                               status,                                 // cmd_status
                               8,                                      // sizeof payload
                               &deviceId                               // response payload
        );

        ll_add_node(CR_LL,
                    &generic_pkt,
                    CMD_RESP_PACKET_SIZE,
                    ti,
                    STORE_DATA);

        xQueueSendToBack(tcp_core_send, generic_pkt, portMAX_DELAY);

        give_master_core_outstanding_commands();
        break;
    case ACK_STRESS_TEST:
        xStatus = xTaskCreate(ack_stress_test,      // function
                              "ack_stress_test",    // name
                              2048,                 // stack size
                              NULL,                 //
                              MASTER_CORE_PRIORITY, // priority
                              NULL);                // handle
        if (xStatus != pdPASS) {
            ESP_LOGI(TAG, "Could not create SEND_MULTI_PART_RSP.. Giving up and restarting");
            esp_restart();
        }

        give_master_core_outstanding_commands();
        break;
    default:
        ESP_LOGI(TAG, "unknown command recived! CMD == %d", cmd_type);
        ASSERT(0);
    }
}

static void master_core_handle_server_packet() {
    ESP_LOGI(TAG, "Handling processed packet");

    // This queue is used as a signalling object, grab the actuall packet from the rx_ll
    int        packet_type;
    BaseType_t xStatus = xQueueReceive(tcp_core_processed_packet, &packet_type, 0);
    if (xStatus != pdPASS) {
        ASSERT(0);
    }

    if (ll_pop(RX_LL, generic_pkt) < 0) {
        ESP_LOGE(TAG, "could not pop RX LL");
        assert(0);
    }

    uint8_t packet = packet_get_type(generic_pkt);
    ESP_LOGI(TAG, "Master Core processed and is going to Fw command of typed %hhu", packet);

    if (packet_type != packet) {
        ESP_LOGE(TAG, "Unexpected packet type popped!, expected %hhu, got %hhu", packet_type, packet);
        ASSERT(0);
    }

    switch (packet) {
    case CMD_PACKET:
        process_cmd_pkt();
        break;
    case ECHO_PACKET:
        process_echo_pkt();
        break;
    case FOTA_PACKET:
        fota_handler();
        break;
    case DATA_PACKET:
        if (!get_fota_underway()) {
            ASSERT(0);
        }
        xStatus = xQueueSendToBack(master_to_fota_q, generic_pkt, MASTER_TO_FOTA_Q_TIME_OUT);
        if (xStatus != pdPASS) {
            ESP_LOGE(TAG, "Timed out sending data packet to master core");
            ASSERT(0);
        }
        break;
    default:
        ESP_LOGE(TAG, "Unknown command rxed");
        ASSERT(0);
    }
}

static void registeration_core(void* v) {
    char device_name[MAX_DEVICE_NAME];
    uint8_t bricked;
    file_core_get(NVS_BRICKED, &bricked);

    for (;;) {
        // send hello packet to let BE know we are online
        xEventGroupWaitBits(tcp_status,   /* The event group being tested. */
                            TCP_UP,       /* The bits within the event group to wait for. */
                            pdTRUE,       /* Clear the TCP_UP bit automatically */
                            pdFALSE,      /* N/A */
                            portMAX_DELAY /* wait forever */
        );

        uint32_t deviceId = get_device_id();
        if (deviceId == MANUFACTURING_DEVICE_ID) {
            ESP_LOGE(TAG, "Could not get device id!, sleeping!");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            break;
        }

        ESP_LOGI(TAG, "Sending hello-packet to BE with DeviceId %lld, bricked status = %hhu", get_device_id(), bricked);
        hello_pkt_t pkt;
        uint16_t    hello_id = create_transaction_id();

        bool err = get_device_name(device_name);
        if (!err) {
            ESP_LOGE(TAG, "Could not get device name, memsetting to zero!");
            memset(device_name, 0, MAX_DEVICE_NAME);
        }

        packet_hello_create(&pkt,
                            hello_id,
                            get_device_id(),
                            device_name,
                            fota_get_fw_version(), 
                            bricked);

        ll_add_node(CR_LL,
                    &pkt,
                    HELLO_PACKET_SIZE,
                    hello_id,
                    STORE_DATA);

        BaseType_t xStatus = xQueueSendToBack(tcp_core_send, &pkt, MASTER_TIMEOUT);
        if (xStatus != pdTRUE) {
            ASSERT(0);
        }


    }
}

static void master_core_process_acks() {
    ack_pkt_t pkt;
    uint8_t   ack_pkt[PACKET_LEN_MAX];

    BaseType_t xStatus = xQueueReceive(tcp_core_send_ack, &pkt, DONT_WAIT_QUEUE);
    if (xStatus != pdPASS) {
        ASSERT(0);
    }

    if (ll_peek(pkt.transaction_id, TRUE, ack_pkt) < 0) {
        // Master core should always store LL's
        ASSERT(0);
    }

    if (packet_get_type(ack_pkt) == HELLO_PACKET) {
        if (packet_ack_get_reason(&pkt) == ACK_GOOD) {
            ESP_LOGI(TAG, "Got an ACK for transaction ID %d", pkt.transaction_id);
            ll_delete(CR_LL, pkt.transaction_id, TRUE);
            set_master_core_status(MASTER_CORE_REGISTERED);
            return;
        } else {
            ESP_LOGW(TAG, "Got a reason=%d for transaction ID %d", packet_ack_get_reason(&pkt), pkt.transaction_id);
            ASSERT(0); //TODO: handle more gracefullly
        }
    }

    if (packet_get_type(ack_pkt) == CMD_RESP_PACKET) {
        if (packet_ack_get_reason(&pkt) == ACK_GOOD) {
            ESP_LOGI(TAG, "Got an ACK for transaction ID %d, type == CMD_RESP_PACKET", pkt.transaction_id);
            ll_delete(CR_LL, pkt.transaction_id, TRUE);
            return;
        } else {
            ESP_LOGW(TAG, "Got a reason=%d for transaction ID %d", packet_ack_get_reason(&pkt), pkt.transaction_id);
            ASSERT(0); //TODO: handle more gracefullly
        }
    }

    if (packet_get_type(ack_pkt) == LOGIN_PACKET) {
        if (packet_ack_get_reason(&pkt) == ACK_GOOD) {
            ESP_LOGI(TAG, "Got an ACK for transaction ID %d, type == LOGIN_PACKET", pkt.transaction_id);
        } else {
            ESP_LOGW(TAG, "Got a reason=%d for transaction ID %d", packet_ack_get_reason(&pkt), pkt.transaction_id);
            print_lcd_api((void*)"Login failed    SERVER DOWN");
        }
        ll_delete(CR_LL, pkt.transaction_id, TRUE);
        return;
    }

    if (packet_get_type(ack_pkt) == FOTA_ACK_PACKET) {
        if (packet_ack_get_reason(&pkt) == ACK_GOOD) {
            ESP_LOGI(TAG, "Got an ACK for transaction ID %d, type == FOTA_ACK_PACKET", pkt.transaction_id);
            ll_delete(CR_LL, pkt.transaction_id, TRUE);
            return;
        } else {
            ESP_LOGW(TAG, "Got a reason=%d for transaction ID %d", packet_ack_get_reason(&pkt), pkt.transaction_id);
            ASSERT(0); //TODO: handle more gracefullly
        }
    }

    if (packet_get_type(ack_pkt) == VOID_PACKET) {
        if (packet_ack_get_reason(&pkt) == ACK_GOOD) {
            ESP_LOGI(TAG, "Got an ACK for transaction ID %d, type == VOID_PACKET", pkt.transaction_id);
            ll_delete(CR_LL, pkt.transaction_id, TRUE);
            return;
        } else {
            ESP_LOGW(TAG, "Got a reason=%d for transaction ID %d", packet_ack_get_reason(&pkt), pkt.transaction_id);
            ASSERT(0); //TODO: handle more gracefullly
        }
    }

    ESP_LOGW(TAG, "Unhandled ACK of type == %d", packet_get_type(ack_pkt));
    ASSERT(0);
}

static void master_core(void* v) {
    // wait for the rest of the subsytems to be live
    while (parallaxCoreReady == 0 || fileCoreReady == 0) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    /* see if there is an operation that did not fully complete last time if we find one
     * we will just erase it (even if it was an add OR erase) */
    uint8_t pending_uid = 0;

    if (file_core_get_journal(&pending_uid)) {
        ESP_LOGW(TAG, "There is a pending operation concerning user = %hu, we will just erase this user", pending_uid);
        delete_specific_user(true, DEL_USER_FLASH_PLUS_PRINT, pending_uid);

        /* to prevent endless loops, we will erase the journal - we could be technically fucked here, good luck me in the future */
        file_core_clear_journal();
    }

    for (;;) {
        QueueHandle_t xActivatedMember = xQueueSelectFromSet(master_core_events, portMAX_DELAY);

        if (xActivatedMember == parallaxLoginQ) {
            master_core_handle_login();
        }

        if (xActivatedMember == tcp_core_processed_packet) {
            master_core_handle_server_packet();
        }

        if (xActivatedMember == tcp_core_send_ack) {
            master_core_process_acks();
        }
    }
}

static void master_core_init_freertos_objects() {
    master_core_events = xQueueCreateSet(MAX_OUTSTANDING_LOGINS + MAX_OUTSTANDING_TCP_CORE_RECV); //TODO SIZE ME!
    xQueueAddToSet(parallaxLoginQ, master_core_events);
    xQueueAddToSet(tcp_core_processed_packet, master_core_events);
    xQueueAddToSet(tcp_core_send, master_core_events);
    xQueueAddToSet(tcp_core_send_ack, master_core_events);

    master_core_protected      = xSemaphoreCreateMutex();  // Used to generate new transaction IDs
    master_core_processing_cmd = xSemaphoreCreateBinary(); //since the thread TAKING the mutex might not always be the same as the one GIVING the mutex,
                                                           // we have to user a binary semapthor
    xSemaphoreGive(master_core_processing_cmd);            //starts in the "taken configurations, must give first"

    //Pushes data packets from master core to FOTA task
    master_to_fota_q    = xQueueCreate(MASTER_TO_FOTA_DEPTH, DATA_PACKET_SIZE); // Internal  ->  FOTA_PACKET
    master_to_suicide_q = xQueueCreate(1, sizeof(int));                         // Master core -> sync task
}

void master_core_spawner() {
    master_core_init_freertos_objects();

    BaseType_t rc;
    rc = xTaskCreate(master_core,
                     "master_core",
                     4096,
                     NULL,
                     MASTER_CORE_PRIORITY,
                     NULL);

    if (rc != pdPASS) {
        assert(0);
    }

    rc = xTaskCreate(registeration_core,
                     "registeration_core",
                     4046,
                     NULL,
                     MASTER_CORE_PRIORITY,
                     NULL);

    if (rc != pdPASS) {
        assert(0);
    }
}
