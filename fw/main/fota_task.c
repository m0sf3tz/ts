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
static const char     TAG[] = "FOTA_CORE";
static uint8_t        generic_pkt[PACKET_LEN_MAX];
static uint8_t        fota_malloc_packet[PAGE_SIZE];
static const uint16_t FW_VERSION = 12; //this is updated PER release.

/**********************************************************
*                    FUCNTIONS 
**********************************************************/

uint16_t fota_get_fw_version() {
    return FW_VERSION;
}

static void create_fota_ack(uint8_t type, uint8_t status) {
    uint16_t ti = create_transaction_id();
    ESP_LOGI(TAG, "writting ACK w/ ID %hu, type %hhu status %hhu", ti, type, status);

    packet_fota_rsp_create(generic_pkt, // Backing array
                           ti,          // Transaction ID
                           type,        // Response type
                           status       // Status
    );

    ll_add_node(CR_LL,
                generic_pkt,
                FOTA_ACK_PACKET_SIZE,
                ti,
                DONT_STORE_DATA);

    send_to_tcp_core(generic_pkt);
}

void fota_task(void* parameters) {
    ESP_LOGI(TAG, "Starting FOTA task");
    set_fota_underway(true);

    BaseType_t         xStatus;
    esp_partition_t*   dst_partition = NULL;
    int                i;
    uint8_t            type = 0;
    uint16_t           crc16_local;
    uint16_t           cur_block = 0;
    esp_err_t          err;
    fota_pkt_payload_t fota_initial, fota_meta, fota_final;

    xStatus = xQueueReceive(master_to_fota_q, generic_pkt, FOTA_TIME_BETWEEN_FOTA_PACKETS);
    if (xStatus != pdPASS) {
        ESP_LOGE(TAG, "Timed out getting initial fota packet");
        ASSERT(0);
    }

    //unpack fota information
    packet_fota_unpack(generic_pkt, &fota_initial);

    ESP_LOGI(TAG, "\n\nSTARTING FOTA!!\n\n");
    ESP_LOGI(TAG, "FW_VERSION = %hu", fota_initial.fw_version);
    ESP_LOGI(TAG, "Magic = %u", fota_initial.magic_marker);
    ESP_LOGI(TAG, "type = %hhu", fota_initial.type);
    ESP_LOGI(TAG, "CRC16 = %hu", fota_initial.fw_crc16);
    ESP_LOGI(TAG, "CRC32 = %u", fota_initial.fw_crc32);
    ESP_LOGI(TAG, "segment = %u", fota_initial.fw_segment);
    ESP_LOGI(TAG, "blocks = %u", fota_initial.fw_blocks);

    // Get partition we will use as fota partition (one of OTA_0 or OTA_1)
    dst_partition = esp_ota_get_next_update_partition(NULL);
    if (!dst_partition) {
        ESP_LOGE(TAG, "dst_partition == 0!");
        ASSERT(0);
    }
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x", dst_partition->subtype, dst_partition->address);

    ESP_LOGI(TAG, "Erasing new OTA partition");
    esp_ota_handle_t ota_handle;
    esp_ota_begin(dst_partition, OTA_SIZE_UNKNOWN, &ota_handle);

    for (; cur_block < fota_initial.fw_blocks; cur_block++) {
        ESP_LOGI(TAG, "Currently fetching block == %hu", cur_block);

        xStatus = xQueueReceive(master_to_fota_q, generic_pkt, FOTA_TIME_BETWEEN_FOTA_PACKETS);
        if (xStatus != pdPASS) {
            ESP_LOGE(TAG, "Timed out getting META FOTA packet, cancelling fota and restarting...");
            create_fota_ack(FOTA_META_ACK, FOTA_STATUS_TIMEDOUT);
            set_fota_underway(false);
            vTaskDelete(NULL);
            return;
        }

        packet_fota_unpack(generic_pkt, &fota_meta);
        if (fota_meta.type != FOTA_META_PACKET) {
            ESP_LOGE(TAG, "Unexpected fota type RXed, %hhu", fota_meta.type);
            create_fota_ack(FOTA_META_ACK, FOTA_STATUS_FAILED_REASON_UNKNOWN);
            ASSERT(0);
        }
        ESP_LOGI(TAG, "RXed a meta packet!");
        ESP_LOGI(TAG, "CRC16 for next 8 segment == %hu", fota_meta.fw_crc16);

        for (i = 0; i < SEGMETNS_PER_BLOCK; i++) {
            xStatus = xQueueReceive(master_to_fota_q, generic_pkt, FOTA_TIME_BETWEEN_FOTA_PACKETS);
            if (xStatus != pdPASS) {
                ESP_LOGE(TAG, "Timed out getting FOTA packet");
                create_fota_ack(FOTA_META_ACK, FOTA_STATUS_FAILED_CRC16);
                set_fota_underway(false);
                vTaskDelete(NULL);
                return;
            }

            ESP_LOGI(TAG, "RXed a data packet!");
            type = packet_get_type(generic_pkt);
            if (type != DATA_PACKET) {
                ESP_LOGE(TAG, "Unexpected packed RXed, %hhu", type);
                create_fota_ack(FOTA_META_ACK, FOTA_STATUS_TIMEDOUT);
                ASSERT(0);
            }

            memcpy(fota_malloc_packet + i * LARGE_PLAYLOAD_SIZE, packet_data_get_payload_data(generic_pkt), LARGE_PLAYLOAD_SIZE);
        }
        crc16_local = crc16(fota_malloc_packet, LARGE_PLAYLOAD_SIZE * SEGMETNS_PER_BLOCK);
        ESP_LOGI(TAG, "CRC16(local) == %hu, CRC16(expected) == %hu", crc16_local, fota_meta.fw_crc16);
        if (crc16_local != fota_meta.fw_crc16) {
            ESP_LOGE(TAG, "CRC16 MISS-MATCH!! - BAILING OUT! - setting fota underway == false!");
            create_fota_ack(FOTA_META_ACK, FOTA_STATUS_FAILED_CRC16);
            goto assert;
        } else {
            create_fota_ack(FOTA_META_ACK, FOTA_STATUS_GOOD);
        }

        //CRC good, commit to memmory: (esp_ota_write won't commit unless magic bit set in payload..)
        if (fota_initial.fw_version >= FW_VERSION_TEST) {
            ESP_ERROR_CHECK(esp_partition_write(dst_partition, cur_block * LARGE_PLAYLOAD_SIZE * SEGMETNS_PER_BLOCK, fota_malloc_packet, SEGMETNS_PER_BLOCK * LARGE_PLAYLOAD_SIZE));
        } else {
            ESP_ERROR_CHECK(esp_ota_write(ota_handle, fota_malloc_packet, SEGMETNS_PER_BLOCK * LARGE_PLAYLOAD_SIZE));
        }
        ESP_LOGI(TAG, "Commited block %hu to memory!", cur_block);
    }

    ESP_LOGI(TAG, "waiting for final packet ! - going to check CRC32!");
    xStatus = xQueueReceive(master_to_fota_q, generic_pkt, FOTA_TIME_BETWEEN_FOTA_PACKETS);
    if (xStatus != pdPASS) {
        ESP_LOGE(TAG, "Timed out getting final fota packet");
        create_fota_ack(FOTA_FINAL_ACK, FOTA_STATUS_TIMEDOUT);
        set_fota_underway(false);
        vTaskDelete(NULL);
        return;
    }

    //unpack fota information
    packet_fota_unpack(generic_pkt, &fota_final);
    if (fota_final.type != FOTA_FINAL_PACKET && fota_final.type != FOTA_FINAL_TEST_ONLY) {
        ESP_LOGE(TAG, "Did not get FINAL_ACK when expecetd, got %hhu instead?", fota_final.type);
        create_fota_ack(FOTA_FINAL_ACK, FOTA_STATUS_FAILED);
        set_fota_underway(false);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Recieved final ack! - going to check CRC32!");
    void*                   mapped_region;
    spi_flash_mmap_handle_t handle;
    ESP_ERROR_CHECK(esp_partition_mmap(dst_partition,
                                       0,
                                       dst_partition->size,
                                       SPI_FLASH_MMAP_DATA,
                                       &mapped_region,
                                       &handle));
    uint32_t crc32_local = crc32(mapped_region, SEGMETNS_PER_BLOCK * fota_initial.fw_blocks * LARGE_PLAYLOAD_SIZE);

    ESP_LOGI(TAG, "CRC32(local) == %u, CRC32(expected) == %u", crc32_local, fota_initial.fw_crc32);
    if (crc32_local != fota_initial.fw_crc32) {
        ESP_LOGE(TAG, "CRC32 MISS-MATCH!!");
        create_fota_ack(FOTA_META_ACK, FOTA_STATUS_FAILED_CRC32);
        goto assert;
    } else {
        if (fota_final.type == FOTA_FINAL_TEST_ONLY) {
            create_fota_ack(FOTA_FINAL_TEST_ACK, FOTA_STATUS_GOOD);
        } else {
            create_fota_ack(FOTA_FINAL_ACK, FOTA_STATUS_GOOD);
        }
    }

    spi_flash_munmap(handle);
    if (fota_final.type == FOTA_FINAL_TEST_ONLY) {
        ESP_LOGI(TAG, "'Tis was only a test!, will not reboot, will kill FOTA task and set FOTA mode off!");
        set_fota_underway(false);
        vTaskDelete(NULL);
    } else {
        esp_ota_end(ota_handle);
    }

    ESP_LOGI(TAG, "FOTA download finished! going to reboot!");
    esp_err_t err_boot = esp_ota_set_boot_partition(dst_partition);
    if (err_boot) {
        ESP_LOGE(TAG, "setting boot failed with error %s", esp_err_to_name(err_boot));
        ESP_LOGE(TAG, "Failed writting to partition subtype %d at offset 0x%x", dst_partition->subtype, dst_partition->address);
        set_fota_underway(false);
        vTaskDelete(NULL);
    }
    int     status;
    uint8_t item = FOTA_COOKIE_RUNNING_NEW_FW;
    status       = file_core_set(NVS_FW_COOKIE, &item);
    if (status != ITEM_GOOD) {
        ESP_LOGE(TAG, "Failed to set new fw cookie!, FUCK!");
        ASSERT(0);
    }

    /* wait a bit and restart */
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    esp_restart();
assert:
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    ASSERT(0);
}

void fota_check_new_fw() {
    int status;
    int new_fw_cookie = 0;
    int item;
    status = file_core_get(NVS_FW_COOKIE, &new_fw_cookie);
    if (status != ITEM_GOOD && status != ITEM_NOT_INITILIZED) {
        ESP_LOGE(TAG, "Failed to GET new fw cookie!, FUCK!, err = %d", status);
        ASSERT(0);
    }

    if (new_fw_cookie == FOTA_COOKIE_RUNNING_NEW_FW) {
        ESP_LOGI(TAG, "Running new FW, mark as valid!");
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err != ESP_OK) {
            ESP_LOGI(TAG, "error setting OTA == %s", esp_err_to_name(err));
        }
        item   = FOTA_COOKIE_NO_PENDING_NEW_FW;
        status = file_core_set(NVS_FW_COOKIE, &item);
    }
}
