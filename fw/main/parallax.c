#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stdbool.h"
#include "string.h"

#include "lcd.h"
#include "parallax.h"
#include "system_defines.h"

static const char TAG[] = "PARALLAX_CORE";

/**********************************************************
*             PARALLAX CORE GLOBAL VARIABLES
**********************************************************/
QueueHandle_t parallaxLoginQ; // --> Outgoing (to master) someone logged in
int           parallaxCoreReady;

/**********************************************************
*            PARALLAX CORE PRIVATE VARIABLES
**********************************************************/
uint8_t                  parallax_core_mode;
static SemaphoreHandle_t parallaxCommandMutex;
static SemaphoreHandle_t parallaxIrqStatus;
static xQueueHandle      gpio_evt_command;
static xQueueHandle      gpio_evt_proximity;
static QueueSetHandle_t  parallax_core_queue_set;
static QueueHandle_t     parallaxCommandQ;
static QueueHandle_t     parallaxCommandQ_res;
static uint8_t           irqStatus; //if IRQs are disabled from the top or not
/**********************************************************
*            PARALLAX CORE FUNCTIONS STATIC
**********************************************************/

// ISR tied to proximity GPIO
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t)arg;
    if (get_irq_state(GET_IRQ_STATE_FROM_IRQ) == DISABLE_IRQS) {
        return;
    }

    if (parallax_core_mode == SCAN_MODE) {
        xQueueSendFromISR(gpio_evt_proximity, &gpio_num, NULL);
    } else {
        xQueueSendFromISR(gpio_evt_command, &gpio_num, NULL);
    }
    gpio_intr_disable(gpio_num);
}

// Put parallax in reset
// reset = 0 ---> take out of reset
// reset = 1 ---> put in reset
static void parallax_reset(int reset) {
    if (reset != 0 && reset != 1) {
        ESP_LOGE(TAG, "INCORRECT VALUE TO PARALLAX RESET");
        return;
    }
    gpio_set_level(RESET_PIN, reset);
}

static void decorate_function(const char* str) {
    printf("\n");
    printf("%s\n", str);
    printf("\n");
}

char checksum(char* packet) {
    // Calculate the checksum
    return (packet[1] ^ packet[2] ^ packet[3] ^ packet[4] ^ packet[5]);
}

// Checks a packet has the correct response
// assumes response is the 5th byte
static uint8_t getResponse(char* packet) {
    return (packet[RESPONSE_BYTE]);
}

// Prints out a packet
static void printPacket(char* command, const char* start_message) {
    int i;

    printf("%s", start_message);
    for (i = 0; i < 8; i++) {
        printf("0x%02x ", command[i]);
    }
    printf("\n");
}

// Sanitize response from device, verify checksum + len
static int validateResponse(int bytes_len, char* packet) {
    if (bytes_len != RESPONSE_LEN) {
        printf("Faled to validate response len, expected %d - got %d \n", RESPONSE_LEN, bytes_len);
        goto reset;
    }

    // calculate checksum and compare to what
    // we read from the device
    if (checksum(packet) != packet[CHECKSUM_BYTE]) {
        printf("Failed to valididate response checksum, calculated %d, actuall %d\n", checksum(packet), packet[CHECKSUM_BYTE]);
        goto reset;
    }

    return true;

reset:
    //give some time for the printf to drain before resetting
    puts("");
    printPacket(packet, "FAILED PACKET:   ");
    printf("...resetting fingerprint device...\n");
    reset_device();
    return false;
}

// This function breaks down the 3 commands we need
// to send to the device into 3 distinct commads,
// to pick a command, pass the state to it,
// so to add user ID 0, make the following calls
//   add_user_state_machine(0x0,0);
//   add_user_state_machine(0x0,1);
//   add_user_state_machine(0x0,2);
static int add_user_state_machine(uint16_t userId, int state) {
    if (state < 0 || state > 3) {
        return 1;
    }

    print_lcd_api((uint8_t*)"Place Finger      on scanner");
    char userIdLow, userIdHigh;
    userIdLow  = 0xFF & userId;
    userIdHigh = (userId >> 8) & 0xFF;

    //we need to send the following combination of commands to add a user
    // [ CMD_GUARD ,CMD_ADD_FINGERPRINT_1,USERID_HIGH, USERID_LOW, USER_PRIV, 0, CHECKSUM, CMD_GUARD ]
    // [ CMD_GUARD ,CMD_ADD_FINGERPRINT_2,USERID_HIGH, USERID_LOW, USER_PRIV, 0, CHECKSUM, CMD_GUARD ]
    // [ CMD_GUARD ,CMD_ADD_FINGERPRINT_3,USERID_HIGH, USERID_LOW, USER_PRIV, 0, CHECKSUM, CMD_GUARD ]
    // Where CMD_ADD_FINGERPRINT_(1/2/3) are equal to 0x1,0x2,0x3.
    // We also need a small wait in between sending the commands

    char command[8];
    char rx_buff[8];
    memset(command, 0, sizeof(command));
    memset(rx_buff, 0, sizeof(rx_buff));

    command[0] = CMD_GUARD;
    command[1] = (state + 1); //CMD_ADD_FINGERPRINT_1 starts at 1, state starts at 0
    command[2] = userIdHigh;
    command[3] = userIdLow;
    command[4] = USR_PRIV;
    command[5] = 0;
    command[6] = checksum(command);
    command[7] = CMD_GUARD;

    printPacket(command, "TX:  ");

    // we sleep for about a second between writes, if we can not
    // write the entire message everytime, this means that the buffer inside
    // the uart-core is not getting drained
    // we will simply restart if we every get to this positon - recovering is too hard
    uart_write_bytes(UART_NUM_1, command, CMD_LEN);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    //read the response and check the status
    int rxBytes = uart_read_bytes(UART_NUM_1, (uint8_t*)rx_buff, CMD_LEN, 2500 / portTICK_PERIOD_MS); // For some reason the write API use char
    // and the read api uses uint8_t... fix it here

    // Validate both len and checksum
    if (!validateResponse(rxBytes, rx_buff)) {
        return FAILED_ADD_FINGERPRINT;
    }

    printPacket(rx_buff, "RX:  ");
    // Validate we got an ACK back, anything else, reset
    char response = getResponse(rx_buff);
    if (response != ACK_SUCCESS) {
        puts("resetting...");
        reset_device();
    }

    printf("Response == %d \n", response);
    return (getResponse(rx_buff));
}

static char add_user(commandQ_parallax_t* cmd) {
    BaseType_t xStatus;
    int        ret;
    int        i;
    int        io_drain;

    // Drain Q, should be empty anyway..
    do {
        xStatus = xQueueReceive(gpio_evt_command, &io_drain, 0);
    } while (xStatus == pdPASS);

    print_lcd_api((void*)"Adding user");
    vTaskDelay(1500 / portTICK_RATE_MS);
    print_lcd_api((void*)"Press Right      button!");

    gpio_intr_enable(GPIO_INPUT_IO_LOGIN);

    for (i = 0; i < 3; i++) {
        BaseType_t xStatus = xQueueReceive(gpio_evt_command, &io_drain, pdMS_TO_TICKS(5000));
        if (xStatus != pdTRUE) {
            print_lcd_api((uint8_t*)"Failed to add user!");
            puts("timed out waiting for proximity ISR!");
            return TIMED_OUT_GETTING_PRINT;
        }
        // Drain Q, should be empty anyway..
        do {
            xStatus = xQueueReceive(gpio_evt_command, &io_drain, 0);
        } while (xStatus == pdPASS);

        ret = add_user_state_machine(cmd->id, i);
        gpio_intr_enable(GPIO_INPUT_IO_LOGIN);
        
        if (ret != ACK_SUCCESS) {
            print_lcd_api((uint8_t*)"Failed to add user!");
            gpio_intr_disable(GPIO_INPUT_IO_LOGIN);

            if (ret == ACK_USER_EXISTS) {
                print_lcd_api((uint8_t*)"User already exists on device!");
            } else {
                print_lcd_api((uint8_t*)"Failed to add user!");
            }
            return ret;
        }
        if (i != 2) {
            print_lcd_api((uint8_t*)"Press Right      button again!");
        }
    }

    print_lcd_api((uint8_t*)"User added       to device!");
    gpio_intr_disable(GPIO_INPUT_IO_LOGIN);
    return ADDED_USER;
}

int parallax_thread_gate(commandQ_parallax_t* cmd) {
    int ret;
    xSemaphoreTake(parallaxCommandMutex, portMAX_DELAY);
    xQueueSend(parallaxCommandQ, cmd, 0);                     // Send the command
    xQueueReceive(parallaxCommandQ_res, &ret, portMAX_DELAY); // Wait for the response
    xSemaphoreGive(parallaxCommandMutex);
    return ret;
}

// Delete SPECIFIC users
static int delete_specific_users(uint16_t user_id) {
    char command[8];
    char rx_buff[8];
    memset(command, 0, sizeof(command));
    memset(rx_buff, 0, sizeof(rx_buff));

    command[0] = CMD_GUARD;
    command[1] = CMD_DELETE_USER;
    command[2] = user_id >> 8 & 0xFF;
    command[3] = user_id & 0xFF;
    command[4] = 0;
    command[5] = 0;
    command[6] = checksum(command);
    command[7] = CMD_GUARD;

    printPacket(command, "TX:  ");

    int txBytes = uart_write_bytes(UART_NUM_1, command, CMD_LEN);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // We know that the chechsum is correct (since we computed it locally)
    // We will just use this function for checking we sent out all our command
    validateResponse(txBytes, command);

    //read the response and check the status
    int rxBytes = uart_read_bytes(UART_NUM_1, (uint8_t*)rx_buff, CMD_LEN, 1000 / portTICK_PERIOD_MS); // For some reason the write API use char
        // and the read api uses uint8_t... fix it here

    // Validate both len and checksum
    validateResponse(rxBytes, rx_buff);
    printPacket(rx_buff, "RX:  ");

    if (getResponse(rx_buff) != ACK_SUCCESS) {
        char response = getResponse(rx_buff);
        printf("Response == %d\n", response);
    }

    return getResponse(rx_buff);
}

// Delete _ALL_ users
static int delete_all_users() {
    decorate_function("Starting to delete all users");

    char command[8];
    char rx_buff[8];
    memset(command, 0, sizeof(command));
    memset(rx_buff, 0, sizeof(rx_buff));

    command[0] = CMD_GUARD;
    command[1] = CMD_DELETE_ALL_USERS;
    command[2] = 0;
    command[3] = 0;
    command[4] = 0;
    command[5] = 0;
    command[6] = checksum(command);
    command[7] = CMD_GUARD;

    printPacket(command, "TX:  ");

    int txBytes = uart_write_bytes(UART_NUM_1, command, CMD_LEN);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // We know that the chechsum is correct (since we computed it locally)
    // We will just use this function for checking we sent out all our command
    validateResponse(txBytes, command);

    //read the response and check the status
    int rxBytes = uart_read_bytes(UART_NUM_1, (uint8_t*)rx_buff, CMD_LEN, 1000 / portTICK_PERIOD_MS); // For some reason the write API use char
        // and the read api uses uint8_t... fix it here

    // Validate both len and checksum
    validateResponse(rxBytes, rx_buff);
    printPacket(rx_buff, "RX:  ");

    if (getResponse(rx_buff) != ACK_SUCCESS) {
        char response = getResponse(rx_buff);
        printf("Response == %d\n", response);
    }

    decorate_function("Finished deleting all users");
    return getResponse(rx_buff);
}

uint16_t parallax_get_user_id_from_buff(char* rx_buff) {
    return rx_buff[2] << 8 | rx_buff[3];
}

// compare 1:1 (check who is trying to identifiy)
static void match_finger_print_to_id(uint16_t* login_id, bool* login_valid) {
    decorate_function("Starting 1:N compare");

    char command[8];
    char rx_buff[8];
    memset(command, 0, sizeof(command));
    memset(rx_buff, 0, sizeof(rx_buff));

    command[0] = CMD_GUARD;
    command[1] = CMD_SCAN_COMPARE_1_TO_N;
    command[2] = 0;
    command[3] = 0;
    command[4] = 0;
    command[5] = 0;
    command[6] = checksum(command);
    command[7] = CMD_GUARD;

    printPacket(command, "TX:  ");

    int txBytes = uart_write_bytes(UART_NUM_1, command, CMD_LEN);

    // We know that the chechsum is correct (since we computed it locally)
    // We will just use this function for checking we sent out all our command
    if (!validateResponse(txBytes, command)) {
        *login_valid = false;
        return;
    }

    vTaskDelay(3000 / portTICK_PERIOD_MS);

    //read the response and check the status
    int rxBytes = uart_read_bytes(UART_NUM_1, (uint8_t*)rx_buff, CMD_LEN, 1000 / portTICK_PERIOD_MS); // For some reason the write API use char
        // and the read api uses uint8_t... fix it here

    // Validate both len and checksum
    if (!validateResponse(rxBytes, rx_buff)) {
        *login_valid = false;
        return;
    }
    printPacket(rx_buff, "RX:  ");
    decorate_function("done 1:N compare");

    if (getResponse(rx_buff) != USER_MATCHED) {
        ESP_LOGI(TAG, "Login error!");
        *login_valid = false;
        return;
    }

    *login_valid = true;
    *login_id    = parallax_get_user_id_from_buff(rx_buff);

    ESP_LOGI(TAG, "User ID: %d logged in", *login_id);
}

// Fetch how many users we have
uint16_t fetchNumberOfUsers() {
    return 0;
}

void reset_device() {
    parallax_reset(PARALLAX_PUT_IN_REST);
    vTaskDelay(pdMS_TO_TICKS(1000));
    parallax_reset(PARALLAX_TAKE_OUT_RESET);
    vTaskDelay(pdMS_TO_TICKS(1000)); //give time for the device to boot
    // Yikes.. fingerprint module seems to send out a dummy byte on reset (power slurp bringing down TX?)
    // Drain RX buf...
    char byteBlackHole[BLACK_HOLE_BYTES];
    uart_read_bytes(UART_NUM_1, (uint8_t*)byteBlackHole, BLACK_HOLE_BYTES, 0);
}

int check_program_mode() {
    int ret = 1;
    ret     = gpio_get_level(GPIO_INPUT_IO_LOGIN);
    vTaskDelay(pdMS_TO_TICKS(1000));
    ret = gpio_get_level(GPIO_INPUT_IO_LOGIN) & ret;

    return !ret;
}

void init_gpio() {
    // ********************* SET UP RESET PIN **********************

    gpio_config_t io_conf;
    memset(&io_conf, 0, sizeof(gpio_config_t));

    //disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = RESET_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);
    //pull device out of reset
    gpio_set_level(RESET_PIN, 1);

    // ******************* SET UP PROXIMITY GPIO (login) *******************

    memset(&io_conf, 0, sizeof(gpio_config_t));
    //interrupt of LOW level
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    //bit mask of the pins, use GPIO5
    io_conf.pin_bit_mask = GPIO_INPUT_LOGIN_PIN_SEL;
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en   = 1;
    io_conf.pull_down_en = 0;
    gpio_config(&io_conf);

    // ******************* SET UP PROXIMITY GPIO (logout) *******************

    memset(&io_conf, 0, sizeof(gpio_config_t));
    //interrupt of LOW level
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    //bit mask of the pins, use GPIO5
    io_conf.pin_bit_mask = GPIO_INPUT_LOGOUT_PIN_SEL;
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //apply settings
    io_conf.pull_up_en   = 1;
    io_conf.pull_down_en = 0;
    gpio_config(&io_conf);

    // ********************** SETUP ISR **************************

    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_INPUT_IO_LOGIN, gpio_isr_handler, (void*)GPIO_INPUT_IO_LOGIN);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_INPUT_LOGOUT_IO, gpio_isr_handler, (void*)GPIO_INPUT_LOGOUT_IO);

    // ********************* SET UP UART ***************************

    const uart_config_t uart_config = {
        .baud_rate  = 19200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_1, 1024, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void parallax_handle_login() {
    uint16_t login_id    = PARALLAX_ERROR_USER_ID;
    bool     login_valid = false;
    int      button;

    BaseType_t xStatus = xQueueReceive(gpio_evt_proximity, &button, 0);
    if (xStatus != pdPASS) {
        ASSERT(0);
    }
    ESP_LOGI(TAG, "Button %d was pressed!", button);

    print_lcd_api((void*)"Place finger  on scanner!");

    match_finger_print_to_id(&login_id, &login_valid);
    if (!login_valid) {
        if (button == GPIO_INPUT_IO_LOGIN) {
            print_lcd_api((void*)"Login failed! - try again");
        } else {
            print_lcd_api((void*)"Logout failed! - try again");
        }
        goto end;
    }

    parallax_login_t login;
    login.id = login_id;
    /* based on the GPIO we got from the ISR, we know if we have a login or logout */
    if (button == GPIO_INPUT_IO_LOGIN) {
        login.signIn = true;
    } else {
        login.signIn = false;
    }

    xStatus = xQueueSend(parallaxLoginQ, &login, 0);
    if (xStatus != pdPASS) {
        ASSERT(0);
    }

end:
    // Debounce switch
    vTaskDelay(250 / portTICK_RATE_MS);
    gpio_intr_enable(button);
}

void parallax_handle_command() {
    int                 ret;
    commandQ_parallax_t commandQ_cmd;
    BaseType_t          xStatus = xQueueReceive(parallaxCommandQ, &commandQ_cmd, 0);
    if (xStatus != pdPASS) {
        ASSERT(0);
    }

    switch (commandQ_cmd.command) {
    case PARALLAX_ADD_USER:
        ESP_LOGI(TAG, "Starting to add user, disabling LOGOUT gpio");
        gpio_intr_disable(GPIO_INPUT_LOGOUT_IO);
        ret = add_user(&commandQ_cmd);
        gpio_intr_enable(GPIO_INPUT_LOGOUT_IO);
        ESP_LOGI(TAG, "Done adding user, enablnig LOGOUT gpio");
        xQueueSend(parallaxCommandQ_res, &ret, 0);
        break;
    case PARALLAX_DLT_ALL:
        ret = delete_all_users();
        xQueueSend(parallaxCommandQ_res, &ret, 0);
        break;
    case PARALLAX_DLT_SPECIFIC:
        ESP_LOGI(TAG, "Starting to remove user: %hu all users", commandQ_cmd.id);
        ret = delete_specific_users(commandQ_cmd.id);
        xQueueSend(parallaxCommandQ_res, &ret, 0);
        ESP_LOGI(TAG, "Done deleting user");
        break;
    }
}

void set_irq_state(uint8_t state) {
    ESP_LOGI(TAG, "Setting irqStatus to %hhu", state);
    BaseType_t xStatus = xSemaphoreTake(parallaxIrqStatus, IRQ_TIMEOUT);
    if (xStatus != pdPASS) {
        ESP_LOGE(TAG, "Timed out getting mutex!");
        ASSERT(0);
    }
    irqStatus = state;
    xSemaphoreGive(parallaxIrqStatus);
}

uint8_t get_irq_state(bool calledFrom) {
    uint8_t ret;
    if (calledFrom == GET_IRQ_STATE_FROM_TASK) {
        BaseType_t xStatus = xSemaphoreTake(parallaxIrqStatus, IRQ_TIMEOUT);
        if (xStatus != pdPASS) {
            ESP_LOGE(TAG, "Timed out getting mutex!");
            ASSERT(0);
        }
        ret = irqStatus;
        xSemaphoreGive(parallaxIrqStatus);
    } else {
        BaseType_t xStatus = xSemaphoreTakeFromISR(parallaxIrqStatus, IRQ_TIMEOUT);
        if (xStatus != pdPASS) {
            ESP_LOGE(TAG, "Timed out getting mutex!");
            ASSERT(0);
        }

        ret = irqStatus;
        xSemaphoreGiveFromISR(parallaxIrqStatus, NULL);
    }
    return ret;
}

void parallax_thread(void* ptr) {
    bool* console_mode = (bool*)ptr;

    if (!*console_mode) {
        ESP_LOGI(TAG, "In normal mode, ENABLE_IRQS will be set");    

        //disable the top level IRQ sequester
        set_irq_state(ENABLE_IRQS);

        // By default in SCAN_MODE (waiting for logins)
        parallax_core_mode = SCAN_MODE;
    }else{
        ESP_LOGI(TAG, "In console mode, ENABLE_IRQS will be NOT be set %hhu", *console_mode);    
    }

    // Reset module after PON
    reset_device();

    //let the rest of the system know we parallax-core is ready
    parallaxCoreReady = 1;

    for (;;) {
        // Wait for...
        // Command (from master core)
        // User login (will come on gpio_evt_queue_proxmity)
        QueueHandle_t xActivatedMember = xQueueSelectFromSet(parallax_core_queue_set, portMAX_DELAY);

        if (xActivatedMember == gpio_evt_proximity) {
            parallax_handle_login();
        } else if (xActivatedMember == parallaxCommandQ) {
            // some commands use the proximity sensor internaly,
            // we need to set the GPIO to command mode so these
            // commands work as expected (mostly sign up)
            parallax_core_mode = COMMAND_MODE;
            parallax_handle_command();
            parallax_core_mode = SCAN_MODE;
            // Re-enable interrupts incase a command forgets to do it
            gpio_intr_enable(GPIO_INPUT_IO_LOGIN);
        } else {
            ASSERT(0);
        }
    }
}

void parallax_core_init_freertos_objects() {
    parallaxLoginQ       = xQueueCreate(MAX_OUTSTANDING_LOGINS, sizeof(parallax_login_t));
    parallaxCommandQ     = xQueueCreate(1, sizeof(commandQ_parallax_t));
    parallaxCommandQ_res = xQueueCreate(1, sizeof(int32_t));
    parallaxCommandMutex = xSemaphoreCreateMutex();
    parallaxIrqStatus    = xSemaphoreCreateCounting(1, 1);
    gpio_evt_command     = xQueueCreate(1, sizeof(uint32_t));
    gpio_evt_proximity   = xQueueCreate(3, sizeof(uint32_t));

    parallax_core_queue_set = xQueueCreateSet(MAX_OUTSTANDING_LOGINS + 3);
    xQueueAddToSet(parallaxCommandQ, parallax_core_queue_set);
    xQueueAddToSet(gpio_evt_proximity, parallax_core_queue_set);
}

void parallax_core_spawner(bool console_mode) {
    BaseType_t rc;

    //needs to be normal, this thread might not exist when parallax_thread starts 
    static bool cm_local;
    cm_local = console_mode;

    rc = xTaskCreate(parallax_thread,
                     "parallax_thread",
                     4096,
                     &cm_local,
                     4,
                     NULL);
    if (rc != pdPASS) {
        assert(0);
    }
}
