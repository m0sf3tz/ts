#pragma once

#include <stdint.h>

/**********************************************************
*                    TCP_CORE STACK SIZES
**********************************************************/
#define TCP_CORE_TX_STACK_SIZE (3048)
#define TCP_CORE_RX_STACK_SIZE (3048)

#define TCP_CORE_STACK_SIZE (2048)

// TCP core provides a global variable to let the rest of the
// sytem know if TCP core is up or not
typedef enum {
    TCP_CORE_DOWN,
    TCP_CORE_UP
} tcp_core_status_e;

// Main entry point into the TCP core
void tcp_core_spawn_main(void);
void tcp_core_init_freertos_objects(void);

// global functions
tcp_core_status_e get_tcp_core_status();

extern QueueHandle_t      tcp_core_send;
extern QueueHandle_t      tcp_core_send_ack;
extern QueueHandle_t      tcp_core_processed_packet;
extern EventGroupHandle_t tcp_status;

/**********************************************************
*                     TCP EVENTGROUP BITS
**********************************************************/

#define TCP_DOWN (1 << 0)
#define TCP_UP   (1 << 1)
