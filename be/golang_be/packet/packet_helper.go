package main

import (
	"bytes"
	"encoding/binary"
	_ "fmt"
	"log"
	"strings"
)

/**********************************************************
*       					Helpers for Packets
*********************************************************/

func get_packet_payload_len(packet_type uint8) int {
	ret := 0
	switch packet_type {
	case DATA_PACKET:
		ret = LARGE_PAYLOAD_SIZE
		break
	case CMD_PACKET:
		ret = MEDIUM_PAYLOAD_SIZE
		break
	case INTERNAL_ACK_PACKET:
		ret = REASON_SIZE
		break
	case LOGIN_PACKET:
		ret = MEDIUM_PAYLOAD_SIZE
		break
	case HELLO_WORLD_PACKET:
		ret = MEDIUM_PAYLOAD_SIZE
		break
	case GOODBYE_WORLD_PACKET:
		ret = SMALL_PAYLOAD_SIZE
		break
	case SERVER_ACK_PACKET:
		ret = SMALL_PAYLOAD_SIZE
		break
	case CMD_RESPONSE_PACKET:
		ret = MEDIUM_PAYLOAD_SIZE
		break
	case ECHO_PACKET:
		ret = MEDIUM_PAYLOAD_SIZE
	case DEVICE_ACK_PACKET:
		ret = SMALL_PAYLOAD_SIZE
		break
	case FOTA_PACKET:
		ret = MEDIUM_PAYLOAD_SIZE
		break
	case FOTA_ACK_PACKET:
		ret = MEDIUM_PAYLOAD_SIZE
		break
	case VOID_PACKET:
		ret = MEDIUM_PAYLOAD_SIZE
		break
	default:
		log.Fatal("Error! Unknown packet type recieved: ", packet_type)
	}
	return ret
}

func get_packet_len(packet_type uint8) int {
	ret := 0
	switch packet_type {
	case DATA_PACKET:
		ret = DATA_PACKET_SIZE
		break
	case CMD_PACKET:
		ret = CMD_PACKET_SIZE
		break
	case INTERNAL_ACK_PACKET:
		ret = ACK_PACKET_SIZE
		break
	case DEVICE_ACK_PACKET:
		ret = ACK_PACKET_SIZE
		break
	case LOGIN_PACKET:
		ret = LOGIN_PACKET_SIZE
		break
	case HELLO_WORLD_PACKET:
		ret = HELLO_PACKET_SIZE
		break
	case GOODBYE_WORLD_PACKET:
		ret = GOODBYE_PACKET_SIZE
		break
	case CMD_RESPONSE_PACKET:
		ret = CMD_RESPONSE_PACKET_SIZE
		break
	case FOTA_PACKET:
		ret = FOTA_PACKET_SIZE
		break
	case FOTA_ACK_PACKET:
		ret = FOTA_ACK_PACKET_SIZE
		break
	case ECHO_PACKET:
		ret = ECHO_PACKET_SIZE
		break
	case VOID_PACKET:
		ret = VOID_PACKET_SIZE
		break
	default:
		log.Fatal("ERRO! Unknown packet type recieved: ", packet_type)
	}
	return ret
}

func cmd_pack(cmd Cmd_payload) []byte {
	buf := new(bytes.Buffer)

	err1 := binary.Write(buf, binary.LittleEndian, cmd.Cmd_type)

	var err2 error
	if cmd.Cmd_payload != nil {
		err2 = binary.Write(buf, binary.LittleEndian, cmd.Cmd_payload)
	}
	if err1 != nil || err2 != nil {
		log.Fatal("binary.Write failed - errors are as follows", err1, err2)
	}

	return buf.Bytes()
}

func create_goodbye_packet(client uint64) Ipc_packet {
	packet := Packet{}

	packet.Consumer_ack_req = 0
	packet.Data = make([]byte, SMALL_PAYLOAD_SIZE)
	packet.Packet_type = GOODBYE_WORLD_PACKET

	ret := Ipc_packet{}
	ret.P = packet
	ret.ClientId = client

	return ret
}

// Converts a golang Packet representation
// into a "c" packet
func packet_pack(packet Packet) []byte {
	buf := new(bytes.Buffer)

	err1 := binary.Write(buf, binary.LittleEndian, uint8(packet.Packet_type))
	err2 := binary.Write(buf, binary.LittleEndian, packet.Transaction_id)
	err3 := binary.Write(buf, binary.LittleEndian, packet.Consumer_ack_req)
	err4 := binary.Write(buf, binary.LittleEndian, packet.Crc)

	if len(packet.Data) < get_packet_payload_len(packet.Packet_type) {
		logger(PRINT_FATAL, "packet smaller than expected", get_packet_payload_len(packet.Packet_type), "but got ", len(packet.Data))
	}

	var err5 error
	if packet.Data != nil {
		err5 = binary.Write(buf, binary.LittleEndian, packet.Data)
	}
	if err1 != nil || err2 != nil || err3 != nil || err4 != nil || err5 != nil {
		log.Fatal("binary.Write failed - errors are as follows", err1, err2, err3, err4)
	}

	return buf.Bytes()
}

// Converts a C packet into a golang packet
func packet_unpack(packed_packet []byte) Packet {
	// first, extract a general_packet (ie, no payload)
	// using binary.read, then later we will extract the data
	var temp_packet Packet_general
	buf := bytes.NewBuffer(packed_packet)

	err := binary.Read(buf, binary.LittleEndian, &temp_packet)
	if err != nil {
		log.Fatal("Failed to unpack packet, error: ", err)
	}

	// Load the Data
	// - first see how large the payload is
	packet_out := Packet{}

	packet_out.Packet_type = temp_packet.Packet_type
	packet_out.Transaction_id = temp_packet.Transaction_id
	packet_out.Consumer_ack_req = temp_packet.Consumer_ack_req
	packet_out.Crc = temp_packet.Crc

	payload_len := get_packet_payload_len(packet_out.Packet_type)
	if payload_len == 0 {
		// This packet does not have a payload
		return packet_out
	}

	if len(packed_packet) < get_packet_payload_len(packet_out.Packet_type) {
		logger(PRINT_FATAL, "len of b[] is smaller than len of packet")
	}

	packet_out.Data = make([]byte, 0, payload_len)
	packet_out.Data = append(packet_out.Data, packed_packet[PAYLOAD_OFFSET:PAYLOAD_OFFSET+payload_len]...)

	return packet_out
}

func hello_packet_unpack(p Packet) hello_packet {
	if p.Packet_type != HELLO_WORLD_PACKET {
		log.Fatal("tried to get the deviceID from a non- hello packet!")
	}

	hp := hello_packet{}
	hp.DeviceId = binary.LittleEndian.Uint64(p.Data[0:8])
	hp.fw_version = binary.LittleEndian.Uint16(p.Data[8:10])
	hp.bricked = p.Data[10]

	nullIndex := strings.Index(string(p.Data[11:]), "\x00")
	if nullIndex > 0 {
		hp.device_name = string(p.Data[11 : 11+nullIndex])
	}
	return hp
}

func create_ack_pack(p Packet, reason uint8) Packet {
	r := Packet{}
	r.Packet_type = SERVER_ACK_PACKET
	r.Consumer_ack_req = CONSUMER_ACK_NOT_NEEDED
	r.Transaction_id = p.Transaction_id
	r.Data = make([]byte, SMALL_PAYLOAD_SIZE)
	r.Data[PAYLOAD_OFFSET_DEVICE_ID] = reason

	return r
}

/**********************************************************
*       					Helpers for Ipc_packets
*********************************************************/

// Converts a golang Ipc_packet representation
// into a "c" Ipc_packet
func ipc_packet_pack(i Ipc_packet) []byte {
	buf := new(bytes.Buffer)

	_, err1 := buf.Write(packet_pack(i.P))
	err2 := binary.Write(buf, binary.LittleEndian, i.ClientId)
	err3 := binary.Write(buf, binary.LittleEndian, i.DeviceId)

	if err1 != nil || err2 != nil || err3 != nil {
		logger(PRINT_FATAL, "binary.Write failed - errors are as follows", err1, err2, err3, "client ID and device ID are ", i.ClientId, i.DeviceId)
	}
	return buf.Bytes()
}

// Converts a "c" Ipc_packet representation
// into a golang Ipc_packet
func ipc_packet_unpack(b []byte) Ipc_packet {
	if b == nil {
		logger(PRINT_FATAL, "nil packet given to ipc-packet_unpack")
	}

	ip := Ipc_packet{}
	ip.P = packet_unpack(b)

	l := get_packet_len(b[0])

	//ugly math... ip.Id starts at (TOTAL_LEN-LEN(INT64)*2)
	id_offset := l                                                       // 16 is len(uint64)*2
	ip.ClientId = binary.LittleEndian.Uint64(b[id_offset : id_offset+9]) // slice notition is [) not []

	id_offset = l + 8
	ip.DeviceId = binary.LittleEndian.Uint64(b[id_offset:])

	return ip
}

func packet_pack_cmd(p Cmd_payload) []byte {
	buf := new(bytes.Buffer)

	err1 := binary.Write(buf, binary.LittleEndian, p.Cmd_type)
	err2 := binary.Write(buf, binary.LittleEndian, p.Cmd_payload)
	if err1 != nil || err2 != nil {
		log.Fatal("binary.Write failed - errors are as follows", err1, err2)
	}
	// Need correct number of pad bytes
	b := buf.Bytes()
	l := len(b)

	//TODO: should check for oversize

	if l == get_packet_payload_len(CMD_PACKET) {
		return b
	}

	if l > get_packet_payload_len(CMD_PACKET) {
		logger(PRINT_FATAL, "CMD payload size exceeded max allowed!")
	}

	// Pad the paylaod to be the correct size
	pad := get_packet_payload_len(CMD_PACKET) - l
	z := make([]byte, pad)
	b = append(b, z...)
	return b
}

func packet_cmd_response_unpack(packed_resp_payload []byte) Cmd_resp_payload {
	if len(packed_resp_payload) != MEDIUM_PAYLOAD_SIZE {
		logger(PRINT_FATAL, "len was = ", len(packed_resp_payload), "but it should have been: ", MEDIUM_PAYLOAD_SIZE)
	}

	var temp_resp Cmd_resp_payload_general
	buf := bytes.NewBuffer(packed_resp_payload)

	err := binary.Read(buf, binary.LittleEndian, &temp_resp)
	if err != nil {
		log.Fatal("Failed to unpack packet, error: ", err)
	}

	packet_out := Cmd_resp_payload{}

	packet_out.Cmd_status = temp_resp.Cmd_status
	packet_out.Transaction_id = temp_resp.Transaction_id

	packet_out.Total_packets = temp_resp.Total_packets
	packet_out.Packets_sequence_number = temp_resp.Packets_sequence_number
	packet_out.Payload_len = temp_resp.Payload_len

	packet_out.Resp_payload = make([]byte, 0, CMD_RESPONSE_PAYLOAD_LEN)
	packet_out.Resp_payload = append(packet_out.Resp_payload, packed_resp_payload[6:]...)

	return packet_out
}

func packet_login_unpack(payload []byte) Login_payload {
	lp := Login_payload{}
	lp.temperature = binary.LittleEndian.Uint16(payload[0:2]) // slice notition is [) not []
	lp.signInOrSignOut = payload[2]
	lp.uid = binary.LittleEndian.Uint32(payload[3:7])
	lp.user_name = string(payload[7:])
	return lp
}

func packet_name_response_unpack(payload []byte) Cmd_name_response {

	rsp := packet_cmd_response_unpack(payload)
	name := Cmd_name_response{}

	name.Name = make([]byte, 0, LARGE_PAYLOAD_SIZE)
	name.Internal_id = binary.LittleEndian.Uint16(rsp.Resp_payload[0:2])  // slice notition is [) not []
	name.Uid = binary.LittleEndian.Uint32(rsp.Resp_payload[2:6])          // slice notition is [) not []
	name.Name = append(name.Name, rsp.Resp_payload[6:rsp.Payload_len]...) /* here we removed the null termination */
	return name
}

func cmd_payload_pack(cmd Cmd_resp_payload) []byte {
	buf := new(bytes.Buffer)

	err1 := binary.Write(buf, binary.LittleEndian, cmd.Cmd_status)
	err2 := binary.Write(buf, binary.LittleEndian, cmd.Transaction_id)
	err3 := binary.Write(buf, binary.LittleEndian, cmd.Total_packets)
	err4 := binary.Write(buf, binary.LittleEndian, cmd.Packets_sequence_number)
	err5 := binary.Write(buf, binary.LittleEndian, cmd.Payload_len)

	var err6 error
	if cmd.Resp_payload != nil {
		err5 = binary.Write(buf, binary.LittleEndian, cmd.Resp_payload)
	}
	if err1 != nil || err2 != nil || err3 != nil || err4 != nil || err5 != nil || err5 != nil || err6 != nil {
		log.Fatal("binary.Write failed - errors are as follows", err1, err2, err3, err4, err5, err6)
	}

	return buf.Bytes()
}

func fota_ack_packet_unpack(p []byte) Fota_ack_packet {
	fap := Fota_ack_packet{}
	fap.Type = p[0]
	fap.status = p[1]
	return fap
}

func fota_ack_packet_pack(fota Fota_ack_packet) []byte {
	buf := new(bytes.Buffer)

	err1 := binary.Write(buf, binary.LittleEndian, fota.Type)
	err2 := binary.Write(buf, binary.LittleEndian, fota.status)

	if err1 != nil || err2 != nil {
		log.Fatal("binary.Write failed - errors are as follows", err1, err2)
	}

	b := buf.Bytes()
	l := len(b)

	if l == get_packet_payload_len(FOTA_ACK_PACKET) {
		return b
	}

	if l > get_packet_payload_len(FOTA_ACK_PACKET) {
		logger(PRINT_FATAL, "fota ack package size exceeded max allowed!")
	}

	// Pad the paylaod to be the correct size
	pad := MEDIUM_PAYLOAD_SIZE - l
	z := make([]byte, pad)
	b = append(b, z...)
	return b
}

func fota_packet_unpack(p []byte) Fota_packet {
	fp := Fota_packet{}
	fp.magic_marker = binary.LittleEndian.Uint32(p[0:4])
	fp.Type = uint8(p[4])
	fp.FW_version = binary.LittleEndian.Uint16(p[5:7])
	fp.FW_CRC32 = binary.LittleEndian.Uint32(p[7:11])
	fp.FW_CRC16 = binary.LittleEndian.Uint16(p[11:13])
	fp.FW_segment = binary.LittleEndian.Uint16(p[13:15])
	fp.FW_blocks = binary.LittleEndian.Uint16(p[15:18])
	return fp
}

func fota_packet_pack(fota Fota_packet) []byte {
	buf := new(bytes.Buffer)

	err1 := binary.Write(buf, binary.LittleEndian, fota.magic_marker)
	err2 := binary.Write(buf, binary.LittleEndian, fota.Type)
	err3 := binary.Write(buf, binary.LittleEndian, fota.FW_version)
	err4 := binary.Write(buf, binary.LittleEndian, fota.FW_CRC32)
	err5 := binary.Write(buf, binary.LittleEndian, fota.FW_CRC16)
	err6 := binary.Write(buf, binary.LittleEndian, fota.FW_segment)
	err7 := binary.Write(buf, binary.LittleEndian, fota.FW_blocks)

	if err1 != nil || err2 != nil || err3 != nil || err4 != nil || err5 != nil || err6 != nil || err7 != nil {
		log.Fatal("binary.Write failed - errors are as follows", err1, err2, err3, err4, err5, err6, err7)
	}

	b := buf.Bytes()
	l := len(b)

	if l == get_packet_payload_len(FOTA_PACKET) {
		return b
	}

	if l > get_packet_payload_len(FOTA_PACKET) {
		logger(PRINT_FATAL, "fota package size exceeded max allowed!")
	}

	// Pad the paylaod to be the correct size
	pad := MEDIUM_PAYLOAD_SIZE - l
	z := make([]byte, pad)
	b = append(b, z...)
	return b
}

func uint64_to_byte_array(deviceId uint64) []byte {
	b := make([]byte, 8)
	binary.LittleEndian.PutUint64(b, uint64(deviceId))
	return b
}

func byte_array_to_uint64(b []byte) uint64 {
	r := binary.LittleEndian.Uint64(b[PAYLOAD_OFFSET : PAYLOAD_OFFSET+9]) // slice notition is [) not []
	return r
}

func create_add_user_payload(name string, id uint32, replace bool) []byte {
	buf := new(bytes.Buffer)

	err1 := binary.Write(buf, binary.LittleEndian, id)
	err2 := binary.Write(buf, binary.LittleEndian, replace)

	if err1 != nil || err2 != nil {
		log.Fatal("binary.Write failed - errors are as follows", err1, err2)
	}

	b := buf.Bytes()

	payload := []byte(name)

	return append(b, payload...)
}

func create_sync_payload(bit_field []byte, max_users int, mode uint8) []byte {
	if len(bit_field) > max_users {
		logger(PRINT_FATAL, "exceeded max users!", len(bit_field))
	}

	pad_len := max_users - len(bit_field)
	pad := make([]byte, pad_len)

	bit_field = append(bit_field, pad...)

	if len(bit_field) != max_users {
		logger(PRINT_FATAL, "expted len to be MAX_USER_SDEVUCE!")
	}

	crc := crc32(bit_field)

	logger(PRINT_NORMAL, "CRC32 ==", crc)

	buf := new(bytes.Buffer)
	err1 := binary.Write(buf, binary.LittleEndian, crc)
	err2 := binary.Write(buf, binary.LittleEndian, mode)
	if err1 != nil || err2 != nil {
		log.Fatal("binary.Write failed - errors are as follows", err1, err2)
	}

	ret := buf.Bytes()
	ret = append(ret, bit_field...)

	return ret
}
