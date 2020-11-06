package main

import "time"

const COMMAND_TIMEOUT_TIME_EXTENDED = (200) // seconds
const COMMAND_TIMEOUT_TIME = (30)           // seconds

const MAX_USERS_DEVICE = (128)
const INVALID_USER_SLOT = (0xFFFF)

const PARALLAX_SIGN_OUT = (0)
const PARALLAX_SIGN_IN = (1)

type employee struct {
	id    uint16 /* which slot teh devices uses */
	name  string
	valid bool
	uid   uint32 /* actuall uid in the backend */
}

type employeeJson struct {
	Id   uint16
	Name string
}

type client struct {
	ClientId    uint64
	fw_version  uint16
	deviceId    uint64
	device_name string
	bricked     uint8
	attach_time time.Time
}

//fota related stuff
const FOTA_START_PACKET = (0)    // Server to device, starts fota process, describes entire operation
const FOTA_START_ACK = (1)       // device to server, Device ready to FOTA
const FOTA_META_PACKET = (2)     // Server to device, describes next 8 packets with CRC16
const FOTA_META_ACK = (3)        // Device to server, Device got the last 8 packets, resend
const FOTA_FINAL_PACKET = (4)    // Server to Device, done sending packets, requests device to reboot and boot off new partition
const FOTA_FINAL_ACK = (5)       // Device -> server, device verfied full CRC32, will reboot
const FOTA_FINAL_TEST_ONLY = (6) // server -> device, done sending packets, requests device to verify CRC (but not reboot)  - for testingo nly
const FOTA_FINAL_TEST_ACK = (7)  // Device -> server, device verfied full CRC32, will not reboot, but will send ack back (for testing)

/*********************************************************
                    Define (status)
********************************************************/
const FOTA_STATUS_GOOD = (0)
const FOTA_STATUS_FAILED = (1)
const FOTA_STATUS_TIMEDOUT = (2)
const FOTA_STATUS_FAILED_SAME_FW = (3)
const FOTA_STATUS_FAILED_CRC16 = (4)
const FOTA_STATUS_FAILED_CRC32 = (5)
const FOTA_STATUS_FAILED_REASON_UNKNOWN = (6)

// If fw version is FOTA_FW_VERSION_MINI_TEST (or larger, we will test different parts of FOTA)

const FOTA_FW_VERSION_MINI_TEST = 0xFF00 /* Test two blocks, no errors */
const SEGMENTS_PER_META_FOTA_PACKET = (8)

const SYNC_USER_DELETED = (0)
const SYNC_USER_EXISTS = (1)

const DONT_CARE_UID = (0)
const SYNC_DELETE_USER_BIT_FIELD = (100)
const SYNC_USER_EXISTS_BIT_FIELD = (200)

const SYNC_NORMAL_MODE = (0)
const SYNC_TEST_MODE = (1)

const DONT_REPLACE_USER = (false)
const REPLACE_USER = (true)

//Bricked codes, must be kept in sync with Fw
const NOT_BRICKED = (0)
const UNKNOWN_LOGIN_BRICKED = (1)
