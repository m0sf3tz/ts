#pragma once

#define LCD_MAX_CHAR          (32)
#define LCD_MAX_CHAR_PER_LINE (16)

#define PRINT_LIFE_SPAN (5000 / portTICK_PERIOD_MS)
#define LCD_MUTEX_WAIT  (5000 / portTICK_PERIOD_MS)

#define LCD_STATE_BOOTING           (0)
#define LCD_STATE_MISSING_NVS_ITEMS (1)
#define LCD_STATE_CONSOLE_MODE      (2)

/* the following must line up with state_core.h*/
#define LCD_STATE_CONNECTING_WIFI  (3) // Connecting to wifi
#define LCD_STATE_CONNECTING_CLOUD (4) // Connected to cloud
#define LCD_STATE_READY            (5) // WIFI + Cloud are up
#define LCD_STATE_BRICKED          (6) // Device is bricked

void lcd_core_spawner(void);
void print_lcd_api(uint8_t* string);
void lcd_core_init_freertos_objects();
void set_lcd_state(uint8_t state);

typedef struct {
    uint8_t len;
    uint8_t msg[LCD_MAX_CHAR];
} lcd_cmd_t;

extern QueueHandle_t lcdPrintQ;
