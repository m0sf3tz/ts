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
#include "stdint.h"
#include <string.h>
#include <sys/param.h>

#include "lcd.h"
#include "state_core.h"
#include "system_defines.h"
#include "file_core.h"

/**********************************************************
*               State Core Local variables
**********************************************************/
static const char        TAG[] = "STATE_CORE";
static uint32_t          state;
static SemaphoreHandle_t state_mutex;

/**********************************************************
*                Functions
**********************************************************/

static void lcd_state_machine_run_internal(uint32_t event) {
    ESP_LOGI(TAG, "Current State %d, event %d", state, event);

    if (state == STATE_WIFI_DOWN) {
        switch (event) {
        case (EVENT_GOT_IP):
            ESP_LOGI(TAG, "State (STATE_WIFI_DOWN) -> (STATE_CONNECTING)");
            state = STATE_CONNECTING;
            goto update_lcd;
        }
    } else if (state == STATE_CONNECTING) {
        switch (event) {
        case (EVENT_WIFI_DISCONNECT):
            ESP_LOGI(TAG, "State (STATE_CONNECTING) -> (SATE_WIFI_DOWN)");
            state = STATE_WIFI_DOWN;
            goto update_lcd;
        case (EVENT_SOCKET_OPENED):
            ESP_LOGI(TAG, "State (STATE_CONNECTING) -> (STATE_CONNECTED_SERVER)");
            state = STATE_CONNECTED_SERVER;
            goto update_lcd;
        }
    } else if (state == STATE_CONNECTED_SERVER) {
        switch (event) {
        case (EVENT_WIFI_DISCONNECT):
            ESP_LOGI(TAG, "State (STATE_CONNECTED_SERVER) -> (SATE_WIFI_DOWN)");
            state = STATE_WIFI_DOWN;
            goto update_lcd;
        case (EVENT_SOCKET_CLOSED):
            ESP_LOGI(TAG, "State (STATE_CONNECTED_SERVER) -> (STATE_CONNECTING)");
            state = STATE_CONNECTING;
            goto update_lcd;
        }
    } else {
        ESP_LOGE(TAG, "Unknown state RX'ed -- asserting!");
        assert(0);
    }

update_lcd:

    switch (state) {
    case (STATE_WIFI_DOWN):
        set_lcd_state(LCD_STATE_CONNECTING_WIFI);
        break;
    case (STATE_CONNECTING):
        set_lcd_state(LCD_STATE_CONNECTING_CLOUD);
        break;
    case (STATE_CONNECTED_SERVER):
        if(file_core_is_bricked()){
          set_lcd_state(LCD_STATE_BRICKED);
        }else {
          set_lcd_state(LCD_STATE_READY);
        }
        break;
    }
}

void lcd_state_machine_run(uint32_t event) {
    if (pdTRUE != xSemaphoreTake(state_mutex, STATE_MUTEX_WAIT)) {
        ESP_LOGE(TAG, "FAILED TO GET STATE_MUTEX!");
        ASSERT(0);
    }

    lcd_state_machine_run_internal(event);

    xSemaphoreGive(state_mutex);
}

void init_state_core_freertos_objects() {
    state       = STATE_WIFI_DOWN;
    state_mutex = xSemaphoreCreateMutex();
}
