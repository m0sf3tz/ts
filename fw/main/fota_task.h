#pragma once

/**********************************************************
*                      Functions    
*********************************************************/

void     fota_task(void*);
uint16_t fota_get_fw_version(void);
void     fota_check_new_fw(void);

/**********************************************************
*                     Define Constants
*********************************************************/
#define FW_PARTITION_SIZE              (0x100000)
#define FOTA_TIME_BETWEEN_FOTA_PACKETS (10000 / portTICK_PERIOD_MS)
#define PAGE_SIZE                      (0x1000)
#define SEGMETNS_PER_BLOCK             (8)
#define FW_VERSION_TEST                (0xFF00)

/**********************************************************
*                     Define (types) 
*********************************************************/

#define FOTA_START_PACKET    (0) // Server to device, starts fota process, describes entire operation
#define FOTA_START_ACK       (1) // device to server, Device ready to FOTA
#define FOTA_META_PACKET     (2) // Server to device, describes next 8 packets with CRC16
#define FOTA_META_ACK        (3) // Device to server, Device got the last 8 packets, resend
#define FOTA_FINAL_PACKET    (4) // Server to Device, done sending packets
#define FOTA_FINAL_ACK       (5) // Device -> server, device verfied full CRC32, ready to reboot
#define FOTA_FINAL_TEST_ONLY (6) // server -> device, done sending packets, requests device to verify CRC (but not reboot)  - for testingo nly
#define FOTA_FINAL_TEST_ACK  (7) // Device -> server, device verfied full CRC32, will not reboot, but will send ack back (for testing)

/*********************************************************
*                     Define (status) 
*********************************************************/

#define FOTA_STATUS_GOOD                  (0)
#define FOTA_STATUS_FAILED                (1)
#define FOTA_STATUS_TIMEDOUT              (2)
#define FOTA_STATUS_FAILED_SAME_FW        (3)
#define FOTA_STATUS_FAILED_CRC16          (4)
#define FOTA_STATUS_FAILED_CRC32          (5)
#define FOTA_STATUS_FAILED_REASON_UNKNOWN (6)
