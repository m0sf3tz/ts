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

#include "console_core.h"
#include "file_core.h"
#include "fota_task.h"
#include "lcd.h"
#include "ll.h"
#include "master_core.h"
#include "parallax.h"
#include "qcore.h"
#include "state_core.h"
#include "tcp_core.h"
#include "wifi_core.h"

#define QUICK_BOOT (0)

void app_main(void) {

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // set up the various linked-lists
    ll_init();

    // init all the queues, semaphores, and other freertos primitives
    parallax_core_init_freertos_objects();
    tcp_core_init_freertos_objects();
    file_core_init_freertos_objects();
    lcd_core_init_freertos_objects();
    init_state_core_freertos_objects();
    wifi_core_init_freertos_objects();
    //print vital NVS items
    file_core_print_details();

    //if new FW, mark as good
    fota_check_new_fw();

    // init lcd
    lcd_core_spawner();

    init_gpio();
    if (check_program_mode()) {
        file_core_spawner();
        parallax_core_spawner(CONSOLE_MODE);
        set_lcd_state(LCD_STATE_CONSOLE_MODE);
        console_init();
        return;
   }
   
    // see if we have the required NVS items, don't start other threads in this case
    int items_good = verify_nvs_required_items();
    if (!items_good) {
        set_lcd_state(LCD_STATE_MISSING_NVS_ITEMS);
        return;
    } else {
        if (!QUICK_BOOT) { 
          /*don't display boot messages and don't backoff boot*/
          lcd_boot_message();
          delay_boot();
        }
    }

    // start the threads
    file_core_spawner();
    tcp_core_spawn_main();
    parallax_core_spawner(NORMAL_MODE);
    master_core_spawner();

    //start wifi
    wifi_connect();
}
