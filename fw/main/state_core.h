#pragma once

#define STATE_MUTEX_WAIT (5000 / portTICK_PERIOD_MS)

#define STATE_WIFI_DOWN        (0) // No Wifi (trying ot connect)
#define STATE_CONNECTING       (1) // Connected to wifi, trying to connect to Backend
#define STATE_CONNECTED_SERVER (2) // Opened port with back end, unit is ready (or bricked)

#define EVENT_GOT_IP          (0) // Connected to WIFI
#define EVENT_WIFI_DISCONNECT (1)
#define EVENT_SOCKET_OPENED   (2) // Connected to back-end
#define EVENT_SOCKET_CLOSED   (3) // Lost server conncetion

// STATE_WIFI_DOWN -> (got ip) -> STATE_CONNECTING -> (socket opened) -> STATE_CONNECTED_SERVER
//      |                               |   |                                         |
//      | _ _  <- (wifi disconnect) - - |   | _ _ _ _ _   <- (socket closed) _ _ _ _  |
//      |                                                                             |
//      | --------------------- <- (wifi disconnect) ---------------------------------|

void lcd_state_machine_run();
void init_state_core_freertos_objects();
