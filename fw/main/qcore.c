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

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "ll.h"
#include "master_core.h"
#include "packet.h"
#include "qcore.h"
#include "state_core.h"
#include "system_defines.h"
#include "tcp_core.h"
#include "timer_helper.h"
#include "wifi_core.h"

//no longer used
#define HOST_IP_ADDR "192.168.0.189"
#define PORT         3334

//real macoy
static uint16_t port;
static char     host_ip[MAX_IP_LEN];

/**********************************************************
*                  TCP CORE GLOBAL VARIABLES
**********************************************************/

QueueHandle_t      tcp_core_send;
QueueHandle_t      tcp_core_send_ack;
QueueHandle_t      tcp_core_processed_packet;
EventGroupHandle_t tcp_status;

tcp_core_status_e tcp_core_status;

/**********************************************************
*                  TCP CORE PRIVATE VARIABLES
**********************************************************/

static QueueHandle_t tcp_core_write_event;
static QueueHandle_t tcp_core_socket_write;
static QueueHandle_t tcp_core_socket_write_ack;
static QueueHandle_t tcp_core_socket_error;
static QueueHandle_t tcp_core_rx_stop;
static QueueHandle_t tcp_core_rx_tx_short_circuit_device_ack;
static QueueHandle_t tcp_core_host_ack;

static QueueSetHandle_t tcp_core_write_queue_set;
static QueueSetHandle_t tcp_core_tx_manager_queue_set;

static SemaphoreHandle_t tcp_core_protected_variables;

static const char TAG[]             = "TCP_CORE";
static int        threads_destroyed = 0;

// curr_buff: tracks the position inside chunk_buffer
// curr_parse_type: what kind of packet is being processed
static int curr_buff;
static int current_parse_type = -1;

/**********************************************************
*                  TCP CORE GLOBAL FUNCTIONS
**********************************************************/

// Provies a way for the rest of the system to know if the TCP core
// is up or not
tcp_core_status_e get_tcp_core_status() {
    tcp_core_status_e ret;
    BaseType_t xStatus = xSemaphoreTake(tcp_core_protected_variables, QCORE_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }

    ret = tcp_core_status;
    xSemaphoreGive(tcp_core_protected_variables);
    return ret;
}

// Sets the TCP core status
static void set_tcp_core_status(tcp_core_status_e stat) {
    BaseType_t xStatus = xSemaphoreTake(tcp_core_protected_variables, QCORE_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }

    tcp_core_status = stat;
    if (stat == TCP_CORE_UP) {
        xEventGroupSetBits(tcp_status, TCP_UP);
    }
    xSemaphoreGive(tcp_core_protected_variables);
}

// the tcp core RX/TX core will call this function
// when they are destroyed, it will increment a protected
// global variable which will be checked by the tcp core
// function to check when both threads are destroyed
static void tcp_core_tx_rx_destroyed() {
    BaseType_t xStatus = xSemaphoreTake(tcp_core_protected_variables, QCORE_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }

    threads_destroyed++;
    xSemaphoreGive(tcp_core_protected_variables);
}

static int tcp_core_tx_rx_destroyed_threads() {
    int ret = 0;
    BaseType_t xStatus = xSemaphoreTake(tcp_core_protected_variables, QCORE_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }

    ret = threads_destroyed;
    xSemaphoreGive(tcp_core_protected_variables);
    return ret;
}

static void tcp_core_tx_rx_reset_destroyed() {
    BaseType_t xStatus =  xSemaphoreTake(tcp_core_protected_variables, QCORE_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }

    threads_destroyed = 0;
    xSemaphoreGive(tcp_core_protected_variables);
}

// host sent us a ACK, handle it
static void chunker_handle_host_ack(void* rx_pkt) {
    static ack_pkt_t ack_nack_packet;
    if (rx_pkt == NULL) {
        ASSERT(0);
    }

    packet_ack_create(&ack_nack_packet,
                      packet_get_transaction_id(rx_pkt),
                      packet_ack_get_reason(rx_pkt),
                      SERVER_ACK_PACKET);

    ESP_LOGI(TAG, "Chunker got transaction ID: %d, was acked/naked with reason %d", packet_get_transaction_id(rx_pkt), packet_ack_get_reason(rx_pkt));

    BaseType_t xStatus = xQueueSendToBack(tcp_core_host_ack, (void* const) & ack_nack_packet, QCORE_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }
}

// Host sent us something other than an ACK, handle it
static void chunker_device_ack_create(void* rx_pkt, uint8_t reason) {
    static ack_pkt_t ack_nack_packet;

    if (rx_pkt == NULL) {
        ASSERT(0);
    }

    // Device does not need to send an ACK back.
    if (packet_get_consumer_ack_req(rx_pkt) == CONSUMER_ACK_NOT_NEEDED) {
        return;
    }

#ifdef TEST_MODE
    if (disable_next_ack) {
        ESP_LOGI(TAG, "Will not send device ack when one is required!");
        disable_next_ack = false;
        return;
    }
#endif
    // Create the packet
    packet_ack_create(&ack_nack_packet,
                      packet_get_transaction_id(rx_pkt),
                      reason,
                      DEVICE_ACK_PACKET);

    ESP_LOGI(TAG, "Sending ACK back to the server for transaction_id = %d", packet_get_transaction_id(rx_pkt));

    BaseType_t xStatus = xQueueSendToBack(tcp_core_rx_tx_short_circuit_device_ack, &ack_nack_packet, QCORE_TIMEOUT); //@TODO deal with negative case
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }
}

void static reset_chunker() {
    curr_buff          = 0;
    current_parse_type = -1;
}

// Splits up a TPC stream into packets
// the rest of the system can understand
static int chunker(const char* rx, const int len) {
    ESP_LOGI(TAG, "Chunker called!");
    // some statics to ease MALLOC ussage
    static char chunk_buff[PACKET_LEN_MAX];

    if (rx == NULL || len == 0) {
        return -1;
    }

    // pckt_size: size of current packet being processed
    // rx_processed : marks the position within the rx buffer;
    static int pckt_size;
    int        rx_proccessed = 0;

    while (rx_proccessed != len) {
        if (current_parse_type == -1) {
            current_parse_type = *(uint8_t*)(rx + rx_proccessed);

            switch (current_parse_type) {
            case DATA_PACKET:
                pckt_size = DATA_PACKET_SIZE;
                break;
            case CMD_PACKET:
                pckt_size = CMD_PACKET_SIZE;
                break;
            case SERVER_ACK_PACKET:
                pckt_size = SERVER_ACK_PACKET_SIZE;
                break;
            case FOTA_PACKET:
                pckt_size = FOTA_PACKET_SIZE;
                break;
#ifdef TEST_MODE
            case ECHO_PACKET:
                pckt_size = ECHO_PACKET_SIZE;
                break;
            case CRASH_PACKET:
                esp_restart();
                break;
            case DONT_SEND_ACK_WHEN_REQUIRED:
                pckt_size = DONT_SEND_ACK_WHEN_REQUIRED_SIZE;
                break;
#endif
            default:
                ESP_LOGE(TAG, "unknown command parse type recieved: %hhu", current_parse_type);
                current_parse_type = -1;
                return -1;
            }
        }

        ESP_LOGI(TAG, "tcp-core starting to parse new msg of type =  %hhu", current_parse_type);
        // Only read within the next message boundary based on the current
        // Packet size being processed.
        int rx_left  = len - rx_proccessed;
        int read_len = (rx_left <= (pckt_size - curr_buff)) ? rx_left : (pckt_size - curr_buff);

        memcpy(chunk_buff + curr_buff, rx + rx_proccessed, read_len);
        curr_buff += read_len;
        rx_proccessed += read_len;

        if (curr_buff > pckt_size) {
            ESP_LOGE(TAG, "currentBuff > pckt_size - huge error");
            ASSERT(0);
        }

        if (curr_buff == pckt_size) {
            //@TODO CHECK CRC!
            // check_packet_crc(...);

            // Check to see if we got a host ack packet,
            // if so, use it to pop the outstanding TX_LL
            if (packet_get_type(chunk_buff) == SERVER_ACK_PACKET) {
                chunker_handle_host_ack(chunk_buff);
                reset_chunker();
            } else {
                ESP_LOGI(TAG, "Adding a packet of type %hhu to the RX_LL, currently has %d", current_parse_type, ll_get_counter(RX_LL));
                int type = current_parse_type;
                reset_chunker();

                // Add packet to RX_LL
                ll_add_node(RX_LL, (void*)chunk_buff, pckt_size, TRANSACTION_ID_DONT_CARE, STORE_DATA);

                BaseType_t xStatus = xQueueSendToBack(tcp_core_processed_packet, (void* const) & type, QCORE_TIMEOUT);
                if (xStatus != pdTRUE) {
                   ASSERT(0);
                }
                // Send a device ACK
                chunker_device_ack_create(chunk_buff, ACK_GOOD); //TODO- don't hardcode ACK_GOOD, depends on CRC!
            }
        }
    }
    return 0;
}

static int sock_connect(int sock) {
    char               addr_str[128];
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(host_ip);
    dest_addr.sin_family      = AF_INET;
    dest_addr.sin_port        = htons(port);

    inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
    int err = connect(sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
        return err;
    }
    int val = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&val, sizeof(val));
    return err;
}

static int sock_create() {
    int addr_family;
    int ip_protocol;

    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;

    int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
    }
    return sock;
}

static void tcp_socket_reader_task(void* pvParameters) {
    ESP_LOGI(TAG, "Starting tcp socket reader task");
    int sock = *((int*)pvParameters);

    for (;;) {
        char rx_buff[PACKET_LEN_MAX];
        int  len = recv(sock, rx_buff, PACKET_LEN_MAX, 0);
        // Error occurred during receiving
        if (len < 0) {
            ESP_LOGE(TAG, "recv failed: errno %d", errno);

            BaseType_t xStatus = xQueueSendToBack(tcp_core_socket_error, (const void*)&errno, QCORE_TIMEOUT);
            if (xStatus == pdPASS) {
                tcp_core_tx_rx_destroyed();
                vTaskDelete(NULL);
            } else {
                ESP_LOGI(TAG, "Something went wrong sending a message to master TCP thread, restarting");
                ASSERT(0);
            }
        }
        // Data received
        else {
            ESP_LOGI(TAG, "Received %d bytes:", len);
            if (len > 0) { // (zero bytes are read on error)
                chunker(rx_buff, len);
            }
        }
    }
}

static void tcp_socket_writer_task_helper_create_ack_nack_and_enqueue(void* tx_pkt, uint8_t reason) {
    static ack_pkt_t ack_nack_packet;
    if (tx_pkt == NULL) {
        ASSERT(0);
    }

    // Device acks don't need to be ACK'd / NAK'd
    if (packet_get_type(tx_pkt) == DEVICE_ACK_PACKET) {
        return;
    }

    // Create the pack
    packet_ack_create(&ack_nack_packet,
                      packet_get_transaction_id(tx_pkt),
                      ACK_GOOD,
                      INTERNAL_ACK_PACKET);
    // See if the device needs to ACK this packet, if so,
    // Set the CONSUMER_ACK_REQ field on the ACK packet
    if (packet_get_consumer_ack_req(tx_pkt) == CONSUMER_ACK_REQUIRED) {
        ack_nack_packet.consumer_ack_req = CONSUMER_ACK_REQUIRED;
    }

    BaseType_t xStatus = xQueueSendToBack(tcp_core_socket_write_ack, &ack_nack_packet, QCORE_TIMEOUT); //@TODO deal with negative case
    if (xStatus != pdTRUE) {
      ASSERT(0);
    }
}

// Manages the direct socket write operations
static void tcp_socket_writer_task(void* pvParameters) {
    ESP_LOGI(TAG, "Starting tcp socket writer task");

    int         sock = *((int*)pvParameters);
    static char tx_buff[PACKET_LEN_MAX];

    for (;;) {
        memset(tx_buff, 0, PACKET_LEN_MAX);
        QueueHandle_t xActivatedMember = xQueueSelectFromSet(tcp_core_write_queue_set, portMAX_DELAY);

        if (xActivatedMember == tcp_core_rx_stop) {
            xQueueReceive(tcp_core_rx_stop, tx_buff, 0);
            tcp_core_tx_rx_destroyed(); // Let the tcp core thread know we are destroyed
            vTaskDelete(NULL);
        }

        BaseType_t xStatus = xQueueReceive(tcp_core_socket_write, &tx_buff, DONT_WAIT_QUEUE);
        if (xStatus != pdPASS) {
            ESP_LOGI(TAG, "Could not read from queue inside tcp_socket_writer_task?");
            ASSERT(0);
        }
        ESP_LOGI(TAG, "Sending transaction_id = %d", packet_get_transaction_id(tx_buff));

        // tx_buff sized for largest possible packet, must send the
        // actuall lenght of the packet
        size_t packet_len = packet_get_size(tx_buff);

        char* tx_ptr = tx_buff;
        for (;;) {
            int sent = 0;
            int len  = send(sock, tx_ptr, packet_len, 0);
            if (len < 0) {
                ESP_LOGE(TAG, "send failed: errno %d", errno);

                // Let the write adaptor know we had an issue sending to the host
                tcp_socket_writer_task_helper_create_ack_nack_and_enqueue(tx_buff, TCP_CORE_DOWN);

                // Let the master TCP core know we are destroyed
                BaseType_t xStatus = xQueueSendToBack(tcp_core_socket_error, (const void*)&errno, QCORE_TIMEOUT);
                if (xStatus == pdPASS) {
                    tcp_core_tx_rx_destroyed(); // Let the tcp core thread know we are destroyed
                    vTaskDelete(NULL);
                } else {
                    ESP_LOGI(TAG, "Something went wrong sending a message to master TCP thread, restarting :(");
                    esp_restart();
                }
            }

            tx_ptr = tx_ptr + len;
            sent   = sent + len;
            if (sent == packet_len) {
                tcp_socket_writer_task_helper_create_ack_nack_and_enqueue(tx_buff, ACK_GOOD);
                break;
            }
        }
    }
}

static void tcp_core_thread(void* pvParameters) {
    int          err;
    BaseType_t   xStatus;
    TaskHandle_t reader, writer;
    reader = NULL;
    writer = NULL;

    for (;;) {

        ESP_LOGI(TAG, "Attempting to bring up TCP core");
        set_tcp_core_status(TCP_CORE_DOWN);
        lcd_state_machine_run(EVENT_SOCKET_CLOSED);

        if (get_wifi_state() == WIFI_DOWN) {
            ESP_LOGI(TAG, "Wifi is down - sleeping and retrying");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        int sock = sock_create();
        if (sock_connect(sock) == 0) {
            ESP_LOGI(TAG, "Successfully connected");
        } else {
            ESP_LOGI(TAG, "Failed to connect - will sleep for 5 seconds and retry...");
            close(sock);
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        // Start the read + writer threads
        xStatus = xTaskCreate(tcp_socket_reader_task, // function
                              "reader_task",          // name
                              TCP_CORE_RX_STACK_SIZE, // stack size
                              (void* const) & sock,   // parameter
                              TCP_CORE_RX_PRIORITY,   // priority
                              &reader);               // handle
        if (xStatus != pdPASS) {
            ESP_LOGI(TAG, "Could not create tcp_socket_reader_thread... Giving up and restarting");
            esp_restart();
        }

        xStatus = xTaskCreate(tcp_socket_writer_task, // function
                              "writer_task",          // name
                              TCP_CORE_TX_STACK_SIZE, // stack size
                              (void* const) & sock,   // parameter
                              TCP_CORE_TX_PRIORITY,   // priority
                              &writer);               // handle
        if (xStatus != pdPASS) {
            ESP_LOGI(TAG, "Could not create tcp_socket_writer_thread... Giving up and restarting");
            esp_restart();
        }

        // Wait just a bit to see if there is a TCP error (must be in tcp read)
        // This little if/else statment is to check the status the
        // TCP connection before we let the rest of the system
        // know TCP is healthy

        vTaskDelay(1000 / portTICK_PERIOD_MS);
        xStatus = xQueueReceive(tcp_core_socket_error, &err, 0);
        if (xStatus == pdPASS) {
            // There was an error in TCP read - socket has issues.
            close(sock);
            xQueueSendToBack(tcp_core_rx_stop, &err, 0);
        } else {
            // No error in setting up TCP, expected pathway, let the rest
            // of the system know TCP is up
            ESP_LOGI(TAG, "TCP_CORE IS UP!");
            set_tcp_core_status(TCP_CORE_UP);

            //Set the LCD state to display that we are connected to the server
            lcd_state_machine_run(EVENT_SOCKET_OPENED);

            // Wait for errors down the line
            xQueueReceive(tcp_core_socket_error, &err, portMAX_DELAY);
            close(sock);
            xQueueSendToBack(tcp_core_rx_stop, &err, 0);
        }
        // Let the rest of the systems know if we have a TCP disconnect
        set_tcp_core_status(TCP_CORE_DOWN);

        // wait for both the RX and the TX socket threads to die, they
        // will increment tcp_core_tx_rx_desroyed_threads by 1 each time
        // one of them destroys itself
        while (tcp_core_tx_rx_destroyed_threads() != 2) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            ESP_LOGI(TAG, "waiting for both tasks to be deleted, %d\n", threads_destroyed);
        }
        ESP_LOGI(TAG, "both tasks deleted");

        // Drain Queues, depending on if TX or RX task failed first,
        // there may or may not be elements in the Queues
        xQueueReceive(tcp_core_socket_error, &err, 0);
        xQueueReceive(tcp_core_rx_stop, &err, 0);

        // Reset the global counter that keeps track of the destroyed threads
        tcp_core_tx_rx_reset_destroyed();
        // Reset the internal chunker variables
        reset_chunker();

        // Wait a little before we retry...
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void tcp_core_serve_master_core() {
    static char send_buff[PACKET_LEN_MAX];
    BaseType_t  xStatus;
    uint16_t    transaction_id;
    ack_pkt_t   ack_nack;

    memset(send_buff, 0, PACKET_LEN_MAX);

    xStatus = xQueueReceive(tcp_core_send, send_buff, DONT_WAIT_QUEUE);
    if (xStatus != pdPASS) {
        //TODO - send a message to master core
        ASSERT(0);
        esp_restart();
    }

    // TCP core is down, get the transaction ID that failed and send it
    // to the master core as a NAK
    if (get_tcp_core_status() == TCP_CORE_DOWN) {
        transaction_id = packet_get_transaction_id(send_buff);
        packet_ack_create(&ack_nack,
                          transaction_id,
                          NAK_TCP_DOWN,
                          INTERNAL_ACK_PACKET);
        xQueueSendToBack(tcp_core_send_ack, &ack_nack, portMAX_DELAY);
        return;
    }

    // This operation can block, it will wait till there is
    // room on the TX buffer

    int ll_r = ll_add_node(TX_LL,                                // LL to add it too
                           send_buff,                            // data to add
                           packet_get_size(send_buff),           // sizeof packet
                           packet_get_transaction_id(send_buff), // Transaction id
                           DONT_STORE_DATA);

    if (ll_r < 0) {
        ASSERT(0);
    }
    // Send to TCP TX socket
    xStatus = xQueueSendToBack(tcp_core_socket_write, send_buff, QCORE_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }
}

static void tcp_core_rx_tx_short_circuit_send_ack() {
    static ack_pkt_t ack_nack_packet;

    BaseType_t xStatus = xQueueReceive(tcp_core_rx_tx_short_circuit_device_ack, &ack_nack_packet, DONT_WAIT_QUEUE);
    if (xStatus != pdPASS) {
        //TODO - send a message to master core
        ASSERT(0);
        esp_restart();
    }

    xStatus = xQueueSendToBack(tcp_core_socket_write, &ack_nack_packet, QCORE_TIMEOUT);
    if (xStatus != pdTRUE) {
        ASSERT(0);
    }
}

// Mangages the TX pathway
static void tcp_core_write_adaptor(void* pvParameters) {
    // Queueset must be sized to hold as many events as the queues it waits on
    QueueSetHandle_t tcp_read_adaptor_queue_set;
    tcp_read_adaptor_queue_set = xQueueCreateSet(MAX_OUTSTANDING_TCP_CORE_SEND_X2 * 2);

    xQueueAddToSet(tcp_core_send, tcp_read_adaptor_queue_set);
    xQueueAddToSet(tcp_core_rx_tx_short_circuit_device_ack, tcp_read_adaptor_queue_set);

    for (;;) {
        QueueHandle_t xActivatedMember = xQueueSelectFromSet(tcp_read_adaptor_queue_set, portMAX_DELAY);
        if (xActivatedMember == tcp_core_send) {
            // Master core is requesting we send out a packet
            tcp_core_serve_master_core();
        } else if (xActivatedMember == tcp_core_rx_tx_short_circuit_device_ack) {
            // RX pathway wants us to send a device_ack
            tcp_core_rx_tx_short_circuit_send_ack();
        } else {
            ASSERT(0);
        }
    }
}

static void tcp_core_write_adaptor_manage_timer_event() {

    node_t*          cur_node;
    ack_pkt_t        ack_nack;
    static char      send_buff[PACKET_LEN_MAX];
    static ack_pkt_t ack_nack_packet;

    // Timer event expired
    BaseType_t xStatus = xQueueReceive(tcp_core_write_event, &ack_nack_packet, DONT_WAIT_QUEUE);
    if (xStatus != pdPASS) {
        //TODO - send a message to master core
        ASSERT(0);
    }

    // Start to walk the TX_LL
    ll_walk_reset(TX_LL);

    for (;;) {
        // Get the current node, automatically advances the internal LL
        cur_node = walk_ll(TX_LL);
        if (cur_node == NULL) {
            return;
        }

        // Is this node outside the retry duration?
        if (timer_expired(cur_node->time_stamp, PACKET_RETRY_DURATION_MS)) {
            // Is this node outside the failure duration?
            // If so, send an NAK to the master core and pop the LL
            if (timer_expired(cur_node->time_stamp, PACKET_FAILED_DURATION_MS)) {
                packet_ack_create(&ack_nack,
                                  cur_node->transaction_id,
                                  SERVER_ACK_TIMED_OUT,
                                  INTERNAL_ACK_PACKET);
                xQueueSendToBack(tcp_core_send_ack, &ack_nack, portMAX_DELAY);
                ESP_LOGW(TAG, "Did not get host ACK within timeout for transaction_id = %d, giving up", cur_node->transaction_id);
                // Don't grab a semaphor, we already have one
                ll_delete(TX_LL, cur_node->transaction_id, DONT_TAKE_SEM);
                return;
            }
#ifdef PACKET_RETRY_MECHANISM
            // Check if the node was internally acked
            // if not this would be strange becuase at minimum a
            // PACKET_RETRY_DURATION_MS must have passed, mark this as a warning
            // but don't do anything else
            if (cur_node->internal_ack == PENDING_ACK) {
                ESP_LOGW(TAG, "Transaction ID %d was not internally acked and we timed out", cur_node->transaction_id);
                return;
            }

            // Otherwise, mark the node as "unacked" and send this node
            // back onto the TX path so we can send it out again
            ESP_LOGW(TAG, "Transaction ID %d has yet to be acked, resending", cur_node->transaction_id);
            cur_node->internal_ack = 0;
            memcpy(send_buff, cur_node->data, cur_node->size); // tcp_core_send is sized for max message len,
                // not doing this would read past smaller packets
                // so we will copy the packet into the max len packet
                // and send that
            ESP_LOGW(TAG, "cur_node->size = %d", cur_node->size);
            xQueueSendToBack(tcp_core_socket_write, send_buff, portMAX_DELAY);
#endif
        }
    }
}

static void tcp_core_write_adaptor_manage_ack_nak_helper() {
    static ack_pkt_t ack_nack_packet;
    uint16_t         transaction_id;

    // Got an ACK/NAK from the tcp writer task
    BaseType_t xStatus = xQueueReceive(tcp_core_socket_write_ack, &ack_nack_packet, DONT_WAIT_QUEUE);
    if (xStatus != pdPASS) {
        //TODO - send a message to master core
        ASSERT(0);
    }

    transaction_id = packet_get_transaction_id(&ack_nack_packet);

    if (packet_ack_get_reason(&ack_nack_packet) != ACK_GOOD) {
        ESP_LOGI(TAG, "Got an NAK for transaction_id %d", transaction_id);

        // POP TX_LL
        ll_delete(TX_LL, transaction_id, TAKE_SEM);

        // Send NAK to master core
        packet_ack_create(&ack_nack_packet,
                          transaction_id,
                          NAK_TCP_DOWN,
                          INTERNAL_ACK_PACKET);
        BaseType_t xStatus = xQueueSendToBack(tcp_core_send_ack, &ack_nack_packet, QCORE_TIMEOUT);
        if (xStatus != pdTRUE) {
          ASSERT(0);
         }

        return;
    }

    // Packet does not need to be acked by the server,
    // since we already sent it out to the TCP core and
    // te TCP core acked us, lets pop it off the TX_LL and
    // send an ack to the host
    if (packet_get_consumer_ack_req(&ack_nack_packet) != CONSUMER_ACK_REQUIRED) {
        ESP_LOGI(TAG, "Consumer ACK not requierd for transaction_id %d, popping LL", transaction_id);

        // POP TX_LL
        ll_delete(TX_LL, transaction_id, TAKE_SEM);

        // Send ACK to master core
        packet_ack_create(&ack_nack_packet,
                          transaction_id,
                          ACK_GOOD,
                          INTERNAL_ACK_PACKET);

        BaseType_t xStatus = xQueueSendToBack(tcp_core_send_ack, &ack_nack_packet, QCORE_TIMEOUT);
        if (xStatus != pdTRUE) {
          ASSERT(0);
        }
        return;
    }

    ESP_LOGI(TAG, "Waiting for consumer ACK on transaction_id %d", transaction_id);
    // If we get here, we need to wait for the host to ACK the packet that was sent out
    // we will mark the packet as internally acked and wait for the server ack to arive.
    ll_modify(transaction_id, INTERNAL_ACK, TX_LL);
}

// host sent us an ACK, use it to
// pop LL
static void tcp_core_handle_host_ack() {
    static ack_pkt_t ack_nack_packet;
    BaseType_t       xStatus = xQueueReceive(tcp_core_host_ack, &ack_nack_packet, DONT_WAIT_QUEUE);
    if (xStatus != pdPASS) {
        ASSERT(0);
    }

    uint16_t transaction_id = ack_nack_packet.transaction_id;

    ESP_LOGI(TAG, "Transaction_id %d, was ACK'd popping LL", transaction_id);

    // POP TX_LL
    ll_delete(TX_LL, transaction_id, TAKE_SEM);

    // Send ACK to master core
    packet_ack_create(&ack_nack_packet,
                      transaction_id,
                      packet_ack_get_reason(&ack_nack_packet),
                      INTERNAL_ACK_PACKET);

    xStatus = xQueueSendToBack(tcp_core_send_ack, &ack_nack_packet, QCORE_TIMEOUT);
     if (xStatus != pdTRUE) {
        ASSERT(0);
    }
}
// listens for acks and nacks incomming from
// A) Recived host side ACK
// B) Internal TCP write ack (packet sent out)
// C) Event timer, time to walk the LL and see if anything is expired.
static void tcp_core_write_tx_ll_manager(void* pvParameters) {
    for (;;) {
        QueueHandle_t xActivatedMember = xQueueSelectFromSet(tcp_core_tx_manager_queue_set, portMAX_DELAY);

        if (xActivatedMember == tcp_core_host_ack) {
            tcp_core_handle_host_ack();
        }

        if (xActivatedMember == tcp_core_socket_write_ack) {
            tcp_core_write_adaptor_manage_ack_nak_helper();
        }

        if (xActivatedMember == tcp_core_write_event) {
            take_ll_sem(TX_LL);
            tcp_core_write_adaptor_manage_timer_event();
            give_ll_sem(TX_LL);
        }
    }
}

static void global_event_core(void* pvParameters) {
    for (;;) {
        int foo = 0;
        xQueueSendToBack(tcp_core_write_event, (const void*)&foo, 0);
        //ESP_LOGI(TAG, "..alive..");
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }
}

void tcp_core_init_freertos_objects() {
    // Global timeback, lets the TX path know it's time
    // to check if it needs to resend a packet
    tcp_core_write_event = xQueueCreate(QUEUE_LEN_ONE, QUEUE_MIN_SIZE); // Incomming  <-  global_event_core

    // TCP core incomming data path + outgoing ACK/NAK Queues
    tcp_core_send             = xQueueCreate(MAX_OUTSTANDING_TCP_CORE_SEND, PACKET_LEN_MAX);     // Incomming  <-  master_core (this is HALF of all other constants to act as a CHOKE)
    tcp_core_send_ack         = xQueueCreate(MAX_OUTSTANDING_TCP_CORE_SEND_X2, ACK_PACKET_SIZE); // Outgoing   ->  master_core
    tcp_core_processed_packet = xQueueCreate(MAX_OUTSTANDING_TCP_CORE_SEND_X2, QUEUE_MIN_SIZE);  // Outgoing   ->  master_core

    // Internal pathways inside the TX pathway
    // tcp_socket_write is used to pass data between tcp_write_adaptor
    // and the tcp_socket_write function. Likewise, the tcp_socket_write_ack
    // is to let the adaptor know that data was sent off to the server
    tcp_core_socket_write     = xQueueCreate(MAX_OUTSTANDING_TCP_CORE_SEND_X2, PACKET_LEN_MAX);  // Internal to tcp_core
    tcp_core_socket_write_ack = xQueueCreate(MAX_OUTSTANDING_TCP_CORE_SEND_X2, ACK_PACKET_SIZE); // Internal to tcp_core

    // Semaphore for helping the tcp socket read/write threads "join"
    // the tcp core task afte they get destroyed
    tcp_core_protected_variables = xSemaphoreCreateMutex();                     // Internal to tcp_core
    tcp_core_socket_error        = xQueueCreate(QUEUE_LEN_ONE, QUEUE_MIN_SIZE); // Internal to tcp_core
    tcp_core_rx_stop             = xQueueCreate(QUEUE_LEN_ONE, QUEUE_MIN_SIZE); // Internal to tcp_core

    // What the tcp socket writter core listens on for
    //  A) Instructions to suicide
    //  B) Incomming packets to send out
    tcp_core_write_queue_set = xQueueCreateSet(QUEUE_LEN_ONE + MAX_OUTSTANDING_TCP_CORE_SEND_X2); // Internal to tcp_core
    xQueueAddToSet(tcp_core_socket_write, tcp_core_write_queue_set);
    xQueueAddToSet(tcp_core_rx_stop, tcp_core_write_queue_set);

    // Used as a short-circuit path from the RX thread to the TX thread, sends out a device
    // ACK back to the host to let it know a packet was successfuly delivered
    tcp_core_rx_tx_short_circuit_device_ack = xQueueCreate(MAX_OUTSTANDING_TCP_CORE_SEND_X2, ACK_PACKET_SIZE);        // Internal to tcp_core
    tcp_core_host_ack                       = xQueueCreate(MAX_OUTSTANDING_TCP_CORE_SEND_X2, SERVER_ACK_PACKET_SIZE); // Internal to tcp_core

    // The TX LL manger task listens on this queue set to either
    //  A) handle a global timer event, walk the tx LL, see if we need to resend a packet or send
    //     to the core
    //  B) The RX core got a server ack, walk the LL and pop the LL
    //  C) The TX core sent out a packet, need to increment the TX_LL send count
    tcp_core_tx_manager_queue_set = xQueueCreateSet(MAX_OUTSTANDING_TCP_CORE_SEND_X2 * 3); // Internal to tcp_core
    xQueueAddToSet(tcp_core_host_ack, tcp_core_tx_manager_queue_set);
    xQueueAddToSet(tcp_core_write_event, tcp_core_tx_manager_queue_set); // Timer callback
    xQueueAddToSet(tcp_core_socket_write_ack, tcp_core_tx_manager_queue_set);

    // setup the TCP core eventgroup
    tcp_status = xEventGroupCreate();

    // Assert if not enough memory to create objects
    ASSERT(tcp_core_write_event);
    ASSERT(tcp_core_send);
    ASSERT(tcp_core_send_ack);
    ASSERT(tcp_core_processed_packet);
    ASSERT(tcp_core_socket_write);
    ASSERT(tcp_core_socket_write_ack);
    ASSERT(tcp_core_protected_variables);
    ASSERT(tcp_core_socket_error);
    ASSERT(tcp_core_rx_stop);
    ASSERT(tcp_core_rx_tx_short_circuit_device_ack);
    ASSERT(tcp_core_host_ack);
    ASSERT(tcp_core_tx_manager_queue_set);
    ASSERT(tcp_status);
}

void tcp_core_spawn_main(void) {
    BaseType_t rc;

    while (fileCoreReady == 0) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    int err = file_core_get(NVS_PORT, &port);
    if (err != ITEM_GOOD) {
        ESP_LOGE(TAG, "can't get port number of server, giving up");
        return;
    }
    err = file_core_get(NVS_IP, host_ip);
    if (err != ITEM_GOOD) {
        ESP_LOGE(TAG, "can't get ip of server, giving up");
        return;
    }

    // Initilize objects
    tcp_core_init_freertos_objects();

    rc = xTaskCreate(global_event_core,
                     "timer_generator",
                     2024,
                     NULL,
                     4,
                     NULL);

    if (rc != pdPASS) {
        assert(0);
    }

    rc = xTaskCreate(tcp_core_thread,
                     "tcp_core",
                     4096,
                     NULL,
                     5,
                     NULL);
    if (rc != pdPASS) {
        assert(0);
    }

    rc = xTaskCreate(tcp_core_write_tx_ll_manager,
                     "tx-manager",
                     2024,
                     NULL,
                     5,
                     NULL);

    if (rc != pdPASS) {
        assert(0);
    }

    rc = xTaskCreate(tcp_core_write_adaptor,
                     "tcp_core write manager",
                     3024,
                     NULL,
                     5,
                     NULL);
    if (rc != pdPASS) {
        assert(0);
    }
}

//heap_caps_print_heap_info(MALLOC_CAP_DEFAULT) ;
