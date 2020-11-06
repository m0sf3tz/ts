package main

import (
	"math/rand"
	"os"
	"strconv"
	"time"
)

const BLOCKS_FOR_QUICK_TEST = (2)
const BLOCKS_FOR_STRESS_TEST = (200)

func fota_aest_same_fw_version(ip Ipc_packet) {
	response_chan := make(chan Ipc_packet)

	//get the FW version for the device
	ok, fw_version := get_device_fw_version(0xdeadbeef)

	ipc, ok := create_ipc_fota_start_packet(0xdeadbeef, fw_version)
	if !ok {
		logger(PRINT_FATAL, "could not open file!")
	}

	site_mux_reg_cmd(0xdeadbeef, response_chan)
	var resp Ipc_packet

	go be_handle_fota(ipc)

	select {
	case resp = <-response_chan:
		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME * 5):
		logger(PRINT_FATAL, "Timed out during same version test!!")
	}
	fap := fota_ack_packet_unpack(resp.P.Data)
	if fap.Type != FOTA_START_PACKET {
		logger(PRINT_FATAL, "incorrect packet type RXed")
	}

	if fap.status != FOTA_STATUS_FAILED_SAME_FW {
		logger(PRINT_FATAL, "incorrect, FW did not complain about updating to same version")
	}

	logger(PRINT_NORMAL, "here", resp)
}

// This function does a fake FOTA
func fota_test(c client) {
	ip := Ipc_packet{}
	ip.DeviceId = c.deviceId
	ip.ClientId = c.ClientId

	//create a fota image, unique each time!
	create_fota_image_test(ip.DeviceId, SEGMENTS_PER_META_FOTA_PACKET*LARGE_PAYLOAD_SIZE*BLOCKS_FOR_QUICK_TEST)

	//setting FOTA_FW_VERSION_MINI_TEST will load a test-routine inside be_handle_fota
	ipc, ok := create_ipc_fota_start_packet(c.deviceId, FOTA_FW_VERSION_MINI_TEST)
	if !ok {
		logger_id(PRINT_FATAL, c.deviceId, "Could not open file!")
	}

	be_handle_fota(ipc)

	logger(PRINT_NORMAL, c.deviceId, "mini fota test done!")
}

func fota(c client, fw_version uint16) {
	ip := Ipc_packet{}
	ip.DeviceId = c.deviceId
	ip.ClientId = c.ClientId

	//create a fota image, unique each time!
	create_fota_image(fw_version)

	ipc, ok := create_ipc_fota_start_packet(c.deviceId, fw_version)
	if !ok {
		logger(PRINT_FATAL, "Could not open file!")
	}

	be_handle_fota(ipc)

	logger(PRINT_NORMAL, "mini fota test done!")
}

func create_fota_image_test(deviceId uint64, size int) {

	deviceIdString := strconv.FormatUint(deviceId, 10)
	deviceIdString += "_minitest"

	os.Remove(deviceIdString)

	f, err := os.Create(deviceIdString)
	if err != nil {
		logger(PRINT_FATAL, "Failed to create!: err = ", err)
	}
	defer f.Close()
	token := make([]byte, size)
	rand.Read(token)
	f.Write(token)
}
