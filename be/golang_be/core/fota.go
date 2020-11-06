package main

import (
	"os"
	"strconv"
	"time"
)

func fota_wait_for_ack(response_chan chan Ipc_packet, deviceId uint64) (bool, uint8, uint8) {
	var rsp Ipc_packet

	select {
	case rsp = <-response_chan:
		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME):
		logger_id(PRINT_WARN, deviceId, "Timed out!")
		return false, 0, 0
		break
	}

	if rsp.P.Packet_type != FOTA_ACK_PACKET {
		logger(PRINT_NORMAL, rsp)
		logger_id(PRINT_FATAL, deviceId, "Did not get a FOTA packet when expecting one!!!!")
	}

	fap := fota_ack_packet_unpack(rsp.P.Data)
	return true, fap.status, fap.Type
}

func end_fota(ipc Ipc_packet, response_chan chan Ipc_packet) {
	set_outstanding_command_or_fota_for_device(ipc.DeviceId, false)
	deregister_command_mux(ipc)
}

func be_handle_fota(ipc Ipc_packet) bool {
	logger(PRINT_NORMAL, "DeviceID: ", ipc.DeviceId, "Staring FOTA!")
	var ok bool
	var status, Type uint8

	initial_fota_packet := fota_packet_unpack(ipc.P.Data)
	final := Ipc_packet{}
	fw_version := initial_fota_packet.FW_version
	logger_id(PRINT_NORMAL, ipc.DeviceId, "Sending fw version:", fw_version)
	ok, clientId := check_device_active(ipc.DeviceId)

	if !ok {
		logger_id(PRINT_WARN, ipc.DeviceId, "Tried to FOTA a dead client")
		return false
	}

	if get_outstanding_command_or_fota_for_device(ipc.DeviceId) {
		logger(PRINT_WARN, "Tried to send fota to a busy client")
		return false
	}
	set_outstanding_command_or_fota_for_device(ipc.DeviceId, true)

	logger(PRINT_NORMAL, "Sending FOTA to DeviceId: ", ipc.DeviceId, " , ClientId: ", clientId)
	ipc.ClientId = clientId

	// First, register with the command mux
	response_chan := make(chan Ipc_packet)
	register_command_mux(ipc, response_chan)

	// Send out the FOTA command, the payload of the FIRST
	// fota packet contains
	// A) MAGIC marker == (0x1337beef)
	// B) FW new version
	// C) FW CRC
	dmq_from_core_to_packet.Send(ipc_packet_pack(ipc))

	initial_fota_rsp := Ipc_packet{}

	select {
	case initial_fota_rsp = <-response_chan:
		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME):
		end_fota(ipc, response_chan)
		return false
		break
	}

	if initial_fota_rsp.P.Packet_type != FOTA_ACK_PACKET {
		// unexpected packet RXed
		status = FOTA_STATUS_FAILED_REASON_UNKNOWN
		end_fota(ipc, response_chan)
		return false
	}

	fap := fota_ack_packet_unpack(initial_fota_rsp.P.Data)
	if fap.status != FOTA_STATUS_GOOD {
		status = fap.status
		Type = FOTA_START_PACKET
		end_fota(ipc, response_chan)
		return false
	}

	for n := 0; n < int(initial_fota_packet.FW_blocks); n++ {
		meta, _ := create_fota_meta_packet(ipc.DeviceId, ipc.ClientId, SEGMENTS_PER_META_FOTA_PACKET*n, fw_version)
		logger_id(PRINT_NORMAL, ipc.DeviceId, "Sending FOTA meta packet!")
		err := dmq_from_core_to_packet.Send(ipc_packet_pack(meta))
		if err != nil {
			logger_id(PRINT_FATAL, ipc.DeviceId, ipc_packet_pack(meta))
		}

		for i := 0; i < SEGMENTS_PER_META_FOTA_PACKET; i++ {
			data, _ := create_fota_data_packet(ipc.DeviceId, ipc.ClientId, i+SEGMENTS_PER_META_FOTA_PACKET*n, fw_version)
			dmq_from_core_to_packet.Send(ipc_packet_pack(data))
		}

		ok, status, Type = fota_wait_for_ack(response_chan, ipc.DeviceId)
		if !ok {
			goto fail
		}

		ok = check_status(FOTA_META_ACK, FOTA_STATUS_GOOD, status, Type, ipc.DeviceId)
		if !ok {
			goto fail
		}
	}

	final = create_ipc_fota_final_packet(ipc.DeviceId, clientId, fw_version)
	dmq_from_core_to_packet.Send(ipc_packet_pack(final))

	ok, status, Type = fota_wait_for_ack(response_chan, ipc.DeviceId)
	if !ok {
		goto fail
	}

	if fw_version >= FOTA_FW_VERSION_MINI_TEST {
		ok = check_status(FOTA_FINAL_TEST_ACK, FOTA_STATUS_GOOD, status, Type, ipc.DeviceId)
		if !ok {
			goto fail
		}
	} else {
		ok = check_status(FOTA_FINAL_ACK, FOTA_STATUS_GOOD, status, Type, ipc.DeviceId)
		if !ok {
			goto fail
		}
	}
	/* let server know we are done */
	end_fota(ipc, response_chan)
	return true

fail:
	if TEST_MODE {
		logger_id(PRINT_FATAL, ipc.DeviceId, "unexpected fail!")
	}
	logger_id(PRINT_WARN, ipc.DeviceId, "Fota Failed...! bailing out!")
	end_fota(ipc, response_chan)
	return false
}

func check_status(expected_type uint8, expected_status uint8, status uint8, Type uint8, id uint64) bool {
	if Type != expected_type {
		if TEST_MODE {
			logger_id(PRINT_FATAL, id, "Fota Failed, got type = ", Type, "while expected", expected_type)
		}
		logger(PRINT_WARN, "Fota Failed, got type = ", Type, "while expected", expected_type)
		return false
	}
	if status != expected_status {
		if TEST_MODE {
			logger(PRINT_FATAL, "Fota Failed, got status  = ", status, "while expected", expected_status)
		}
		logger(PRINT_WARN, "Fota Failed, got status  = ", status, "while expected", expected_status)
		return false
	}
	return true
}

func create_ipc_fota_start_packet(DeviceId uint64, fw_version uint16) (Ipc_packet, bool) {
	var f *os.File
	var err error
	if fw_version >= FOTA_FW_VERSION_MINI_TEST {
		deviceIdString := strconv.FormatUint(DeviceId, 10)
		deviceIdString += "_minitest"
		f, err = os.Open(deviceIdString)
		if err != nil {
			logger(PRINT_FATAL, "could not open file, err =", err)
		}
	} else {
		fota_source := "./fw_versions/timeScan_" + strconv.Itoa(int(fw_version)) + "_aligned.bin"
		f, err = os.Open(fota_source)
		if err != nil {
			logger(PRINT_FATAL, "Could not open FOTA file!")
			return Ipc_packet{}, false
		}
	}

	// Get file size
	fi, err := f.Stat()
	if err != nil {
		logger(PRINT_FATAL, "could not get file stat!, err =", err)
	}

	size := fi.Size()
	logger(PRINT_NORMAL, "FOTA image size ==", size)
	if 0 == size {
		logger(PRINT_FATAL, "FOTA image size == 0")
	}

	if size%(SEGMENTS_PER_META_FOTA_PACKET*LARGE_PAYLOAD_SIZE) != 0 {
		logger(PRINT_FATAL, "FOTA image size not one block size!")
	}

	buf := make([]byte, size)
	_, err = f.ReadAt(buf, 0)
	if err != nil {
		logger(PRINT_FATAL, "Could not read file for FOTA!")
	}

	fp := Fota_packet{}
	fp.FW_version = fw_version
	fp.FW_blocks = uint16(size / (SEGMENTS_PER_META_FOTA_PACKET * LARGE_PAYLOAD_SIZE))
	fp.FW_CRC32 = crc32(buf)
	fp.Type = FOTA_START_PACKET

	ipc := Ipc_packet{}
	ipc.DeviceId = DeviceId
	ipc.P.Packet_type = FOTA_PACKET

	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED

	ipc.P.Data = fota_packet_pack(fp)

	return ipc, true
}

func create_ipc_fota_final_packet(DeviceId uint64, clientId uint64, fw_version uint16) Ipc_packet {
	ipc := Ipc_packet{}
	ipc.DeviceId = DeviceId
	ipc.ClientId = clientId
	ipc.P.Packet_type = FOTA_PACKET

	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED

	fp := Fota_packet{}
	if fw_version >= FOTA_FW_VERSION_MINI_TEST {
		fp.Type = FOTA_FINAL_TEST_ONLY
	} else {
		fp.Type = FOTA_FINAL_PACKET
	}

	ipc.P.Data = fota_packet_pack(fp)

	return ipc
}

func create_fota_data_packet(DeviceId uint64, ClientId uint64, segment int, fw_version uint16) (Ipc_packet, bool) {
	var f *os.File
	var err error
	if fw_version >= FOTA_FW_VERSION_MINI_TEST {
		deviceIdString := strconv.FormatUint(DeviceId, 10)
		deviceIdString += "_minitest"
		f, err = os.Open(deviceIdString)
		if err != nil {
			logger(PRINT_FATAL, "could not open file, err =", err)
		}
	} else {
		fota_source := "./fw_versions/timeScan_" + strconv.Itoa(int(fw_version)) + "_aligned.bin"
		f, err = os.Open(fota_source)
		if err != nil {
			logger(PRINT_FATAL, "Could not open FOTA file = ", fota_source)
			return Ipc_packet{}, false
		}
	}

	ipc := Ipc_packet{}
	ipc.DeviceId = DeviceId
	ipc.ClientId = ClientId

	ipc.P.Packet_type = DATA_PACKET
	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED

	buf := make([]byte, LARGE_PAYLOAD_SIZE)
	n, err := f.ReadAt(buf, int64(segment*LARGE_PAYLOAD_SIZE))
	if n != LARGE_PAYLOAD_SIZE {
		logger(PRINT_FATAL, "did not read LARGE_PAYLOAD_SIZE!, read =", n)
	}
	ipc.P.Data = buf

	return ipc, true
}

func create_fota_meta_packet(DeviceId uint64, ClientId uint64, segment int, fw_version uint16) (Ipc_packet, bool) {
	var f *os.File
	var err error
	if fw_version >= FOTA_FW_VERSION_MINI_TEST {
		deviceIdString := strconv.FormatUint(DeviceId, 10)
		deviceIdString += "_minitest"
		f, err = os.Open(deviceIdString)
		if err != nil {
			logger(PRINT_FATAL, "could not open file, err =", err)
		}
	} else {
		fota_source := "./fw_versions/timeScan_" + strconv.Itoa(int(fw_version)) + "_aligned.bin"
		f, err = os.Open(fota_source)
		if err != nil {
			logger(PRINT_WARN, "Could not open FOTA fine!")
			return Ipc_packet{}, false
		}
	}

	ipc := Ipc_packet{}
	ipc.DeviceId = DeviceId
	ipc.ClientId = ClientId
	ipc.P.Packet_type = FOTA_PACKET

	buf := make([]byte, LARGE_PAYLOAD_SIZE*SEGMENTS_PER_META_FOTA_PACKET)
	n, err := f.ReadAt(buf, int64(segment*LARGE_PAYLOAD_SIZE))
	if n != LARGE_PAYLOAD_SIZE*SEGMENTS_PER_META_FOTA_PACKET {
		logger(PRINT_FATAL, "did not read LARGE_PAYLOAD_SIZE!, read =", n)
	}
	if err != nil {
		logger(PRINT_FATAL, "Failed to read a block from memory", err)
	}

	fap := Fota_packet{}
	fap.Type = FOTA_META_PACKET
	fap.FW_CRC16 = crc16(buf)
	logger(PRINT_NORMAL, "DeviceID: ", DeviceId, "Sending out 8 packets with combined CRC16", fap.FW_CRC16)

	ipc.P.Data = fota_packet_pack(fap)

	return ipc, true
}

func create_fota_ack_packet(DeviceId uint64, ClientId uint64, status uint8, Type uint8) Ipc_packet {
	ipc := Ipc_packet{}
	ipc.DeviceId = DeviceId
	ipc.ClientId = ClientId

	ipc.P.Packet_type = FOTA_ACK_PACKET

	fap := Fota_ack_packet{}
	fap.status = status
	fap.Type = Type

	ipc.P.Data = fota_ack_packet_pack(fap)

	return ipc
}

func create_fota_image(fw_version uint16) bool {
	fota_source := "./fw_versions/timeScan_" + strconv.Itoa(int(fw_version)) + ".bin"
	logger(PRINT_NORMAL, "using fota_source = ", fota_source)
	f, err := os.Open(fota_source)
	if err != nil {
		logger(PRINT_FATAL, "Could not open FOTA file!")
		return false
	}

	fi, err := f.Stat()
	if err != nil {
		logger(PRINT_FATAL, "Could not open FOTA info!")
		return false
	}

	buf := make([]byte, fi.Size())
	f.Read(buf)

	for {
		/* loop goes BRRR*/
		if len(buf)%4096 == 0 {
			break
		}
		buf = append(buf, 0)
	}

	fota_source_aligned := "./fw_versions/timeScan_" + strconv.Itoa(int(fw_version)) + "_aligned.bin"
	logger(PRINT_NORMAL, "Creating", fota_source_aligned)
	fa, err := os.Create(fota_source_aligned)
	if err != nil {
		logger(PRINT_FATAL, "Failed to create!: err = ", err)
	}
	defer fa.Close()
	fa.Write(buf)
	return true
}
