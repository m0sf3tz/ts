#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "stdint.h"
#include <string.h>
#include <sys/param.h>

#include "timer_helper.h"

uint64_t timer_get_ms_since_boot() {
    uint64_t time = esp_timer_get_time();
    time          = time / US_IN_MS;

    return time;
}

uint64_t timer_diff(uint64_t time_a, uint64_t time_b) {
    return (time_b - time_a);
}

//check if time_in_ms + expiration_period_in_ms is greater than current time,
//returns 1 if it is
bool timer_expired(uint64_t time_in_ms, uint64_t expiration_period_in_ms) {
    // need to be careful, can overflow...
    uint64_t diff = timer_diff(time_in_ms, timer_get_ms_since_boot());
    if (diff > expiration_period_in_ms) {
        return 1;
    }
    return 0;
}
