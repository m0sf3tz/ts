#pragma once

/**********************************************************
*                    MISC GLOBAL DEFINES
**********************************************************/
#define TRANSACTION_ID_DONT_CARE (0)
#define DONT_CARE_QUEUE_VALUE    (0x1337BEEF)

/**********************************************************
*                    Thread Priorities 
**********************************************************/
// TCP core priorities
#define TCP_CORE_PRIORITY    (3)
#define TCP_CORE_RX_PRIORITY (2)
#define TCP_CORE_TX_PRIORITY (2)

// MASTER core prioritites
#define MASTER_CORE_PRIORITY (2)

// Callback priority
#define GLOBAL_CALLBACK_PRIORITY (4)

/**********************************************************
*                  Global object sizes 
**********************************************************/
#define GLOBAL_TIMER_CALLBACK_LEN (4)

/**********************************************************
*                       HELPERS 
**********************************************************/
#define ASSERT(x)                                                       \
    do {                                                                \
        if (!(x)) {                                                     \
            ESP_LOGE(TAG, "ASSERT! error %s %u\n", __FILE__, __LINE__); \
            for (;;) {                                                  \
                esp_restart();                                          \
            }                                                           \
        }                                                               \
    } while (0)

#define TRUE  (1)
#define FALSE (0)

/**********************************************************
*                    FEATURES
**********************************************************/
//#define PACKET_RETRY_MECHANISM   //If set, TCP core will resend packets out that have not been acked.

// If set to yes, test features are compiled in
#define TEST_MODE
