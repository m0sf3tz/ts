package main

import (
	"math/rand"
	"time"
)

func disconnect_all(c client) {
	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)

	ipc := create_ipc_cmd_disconnect(c.deviceId, c.ClientId)

	go be_handle_command(ipc)

	select {
	case resp := <-response_chan:
		cmd_rsp := packet_cmd_response_unpack(resp.P.Data)

		if cmd_rsp.Cmd_status != CMD_STATUS_GOOD {
			logger(PRINT_FATAL, "Test failed!, expected CMD_RESPONSE_GOOD got", cmd_rsp.Cmd_status)
		}

		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME):
		logger(PRINT_FATAL, "Timed out resetting device!")
	}

	site_mux_unreg_cmd(c.deviceId)
}

func test_set_device_id(c client) {
	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)

	rand.Seed(time.Now().UnixNano())

	ipc := create_ipc_cmd_set_id(500)
	ipc.ClientId = c.ClientId
	ipc.DeviceId = c.deviceId

	go be_handle_command(ipc)

	var resp Ipc_packet
	select {
	case resp = <-response_chan:
		cmd_rsp := packet_cmd_response_unpack(resp.P.Data)

		if cmd_rsp.Cmd_status != CMD_STATUS_GOOD {
			logger(PRINT_FATAL, "Test failed!, expected CMD_RESPONSE_GOOD got", cmd_rsp.Cmd_status)
		}

		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME):
		logger(PRINT_FATAL, "Timed out resetting device!")
	}

	site_mux_unreg_cmd(c.deviceId)
}

func test_get_device_id(c client) {
	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)

	ipc := create_ipc_cmd_get_id()
	ipc.ClientId = c.ClientId
	ipc.DeviceId = c.deviceId

	go be_handle_command(ipc)

	var resp Ipc_packet
	select {
	case resp = <-response_chan:
		cmd_rsp := packet_cmd_response_unpack(resp.P.Data)

		if cmd_rsp.Cmd_status != CMD_STATUS_GOOD {
			logger_id(PRINT_FATAL, c.deviceId, "Test failed!, expected CMD_RESPONSE_GOOD got", cmd_rsp.Cmd_status)
		}

		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME):
		logger_id(PRINT_FATAL, c.deviceId, "Timed out resetting device!")
	}

	fetched_id := byte_array_to_uint64(resp.P.Data)
	logger_id(PRINT_NORMAL, c.deviceId, "DeviceID == ", fetched_id)
	site_mux_unreg_cmd(c.deviceId)
}

func create_ipc_cmd_disconnect(DeviceId uint64, clientId uint64) Ipc_packet {
	cmd := Cmd_payload{}
	cmd.Cmd_payload = []byte("n/a")
	cmd.Cmd_type = DISCONNECT_CMD

	ipc := Ipc_packet{}
	ipc.DeviceId = DeviceId
	ipc.ClientId = clientId
	ipc.P.Packet_type = CMD_PACKET

	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED

	ipc.P.Data = packet_pack_cmd(cmd)

	return ipc
}

func create_ipc_cmd_set_id(DeviceId uint64) Ipc_packet {
	cmd := Cmd_payload{}
	cmd.Cmd_payload = uint64_to_byte_array(DeviceId)
	cmd.Cmd_type = SET_DEVICE_ID_TEST

	ipc := Ipc_packet{}
	ipc.P.Packet_type = CMD_PACKET

	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED

	ipc.P.Data = packet_pack_cmd(cmd)

	return ipc
}

func create_ipc_cmd_get_id() Ipc_packet {
	cmd := Cmd_payload{}
	cmd.Cmd_payload = []byte("n/a")
	cmd.Cmd_type = GET_DEVICE_ID_TEST

	ipc := Ipc_packet{}
	ipc.P.Packet_type = CMD_PACKET

	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED

	ipc.P.Data = packet_pack_cmd(cmd)

	return ipc
}
