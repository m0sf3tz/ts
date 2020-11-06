#include "assert.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/param.h>

#include "file_core.h"
#include "fota_task.h"
#include "ll.h"
#include "master_core.h"
#include "packet.h"
#include "qcore.h"
#include "system_defines.h"

/**********************************************************
*              FOTA CORE STATIC VARIABLES
**********************************************************/

/**********************************************************
*                    FUCNTIONS 
**********************************************************/
