#pragma once

//TODO: remove =)
//blue - tx (device pov)
//green -rx (deive pov)

//**********************************************************
//                    Parallax related
//*********************************************************
#define RESPONSE_BYTE 0x04
#define CHECKSUM_BYTE 0x06
#define USR_PRIV      0x01 // Every user has the same privilage of 1
// The device does NOT ACK or NACK a 1:N compare, but gives out the user priv OR ACK_NOUSER/ACK_TIMEOUT
// instead in the CMD_RESPONSE slot - so by fixing the USER_PRIV to "1" we can find of use it as a command ACK
#define USER_MATCHED USR_PRIV
#define CMD_GUARD    0xF5 // Start and end of a command
#define CMD_LEN      0x08
#define RESPONSE_LEN (CMD_LEN)

#define ACK_SUCCESS     0x00 // Operation successfully
#define ACK_FAIL        0x01 // Operation failed
#define ACK_FULL        0x04 // Fingerprint database is full
#define ACK_NOUSER      0x05 // No such user
#define ACK_USER_EXISTS 0x07 // Already exists
#define ACK_TIMEOUT     0x08 // Acquisition timeout
#define ACK_ID_EXIST    0x06 // USER_ID exists

#define CMD_SLEEP                        0x2C // Puts the device to sleep
#define CMD_SET_MODE                     0x2D
#define CMD_ADD_FINGERPRINT_1            0x01
#define CMD_ADD_FINGERPRINT_2            0x02
#define CMD_ADD_FINGERPRINT_3            0x03
#define CMD_DELETE_USER                  0x04
#define CMD_DELETE_ALL_USERS             0x05
#define CMD_GET_USERS_COUNT              0x09
#define CMD_SCAN_COMPARE_1_TO_1          0x0B
#define CMD_SCAN_COMPARE_1_TO_N          0x0C
#define CMD_READ_USER_PRIVLAGE           0x0A
#define CMD_SENSITIVITY                  0x28
#define CMD_SCAN_GET_IMAGE               0x24
#define CMD_SCAN_GET_EIGENVALS           0x23
#define CMD_SCAN_PUT_EIGENVALS           0x44
#define CMD_PUT_EIGENVALS_COMPARE_1_TO_1 0x42
#define CMD_PUT_EIGENVALS_COMPARE_1_TO_N 0x43
#define CMD_GET_USER_EIGENVALS           0x31
#define CMD_PUT_USER_EIGENVALS           0x41
#define CMD_GET_USERS_INFO               0x2B
#define CMD_SET_SCAN_TIMEOUT             0x2E // Set the timeout, multiples of ~.25 seconds

// Misc
// Used to suck bytes out of UART RX buffer after a reset since sprurious bytes are recieved
#define BLACK_HOLE_BYTES (32)

//*************************************
//  Parallax-Core Special user-id
//*************************************
#define PARALLAX_ERROR_USER_ID (0xFFFF)

//*************************************
//   Requests to Parallax-Core
//*************************************
#define PARALLAX_ADD_USER     (0)
#define PARALLAX_CMP_USER     (1)
#define PARALLAX_DLT_ALL      (2)
#define PARALLAX_DLT_SPECIFIC (3)

//*************************************
//   Responses to Parallax-Core
//*************************************
#define ADDED_USER              (0)
#define FAILED_ADD_FINGERPRINT  (1)
#define FAILED_ADD_USER_EXISTS  (2)
#define TIMED_OUT_GETTING_PRINT (3)

//*************************************
//   GPIO related stuff for Parralax
//*************************************
#define RESET_PIN     (33)
#define RESET_PIN_SEL (1ULL << RESET_PIN)

// UART pins
#define TXD_PIN (GPIO_NUM_4)
#define RXD_PIN (GPIO_NUM_5)

// (Login) GPIO
#define GPIO_INPUT_IO_LOGIN      (35)
#define GPIO_INPUT_LOGIN_PIN_SEL (1ULL << GPIO_INPUT_IO_LOGIN)
#define ESP_INTR_FLAG_DEFAULT    (0)

// (logout) GPIO
#define GPIO_INPUT_LOGOUT_IO      (34)
#define GPIO_INPUT_LOGOUT_PIN_SEL (1ULL << GPIO_INPUT_LOGOUT_IO)

// Take Parallax in/out of reset
#define PARALLAX_PUT_IN_REST    (0)
#define PARALLAX_TAKE_OUT_RESET (1)

//*************************************
//        TOP level control of IRQ
//*************************************
// IRQs stillh happen, but they don't do anything
// code is like this since there is some logic
// to which IRQ is still enabled/disabled, and
// hammering off IRQs would interfere with them
#define DISABLE_IRQS (0)
#define ENABLE_IRQS  (1)

#define GET_IRQ_STATE_FROM_IRQ  (0)
#define GET_IRQ_STATE_FROM_TASK (1)

#define IRQ_TIMEOUT (2000 / portTICK_PERIOD_MS)
//*************************************
//        Parallax-core modes
//*************************************
// This mode is used to configure the GPIO isr
#define COMMAND_MODE 0 // Responding to a command from the master core
#define SCAN_MODE    1 // Listening to proximity sensor

#define CONSOLE_MODE (true)  // won't handle logins
#define NORMAL_MODE  (false) // will handle logins
//*************************************
//        Parallax-core constants
//*************************************
#define MAX_OUTSTANDING_LOGINS (5)

// globals
extern int           parallaxCoreReady;
extern QueueHandle_t parallaxLoginQ;

// typedefs
typedef struct
{
    uint32_t command;
    uint32_t id;
} commandQ_parallax_t;

typedef struct
{
    uint32_t id;
    bool     signIn;
} __attribute__((packed)) parallax_login_t;

// functions
uint16_t fetchNumberOfUsers();
void     reset_device();
int      parallax_thread_gate(commandQ_parallax_t* cmd);
void     parallax_core_spawner(bool console_mode);
void     parallax_core_init_freertos_objects(void);
void     set_irq_state(uint8_t state);
uint8_t  get_irq_state(bool calledFrom);

int  check_program_mode();
void init_gpio();
