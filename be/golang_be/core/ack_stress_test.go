package main

import (
	"time"
)

func ack_stress_test(c client) {

	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)
	ipc := create_ipc_server_side_ack_stress_test(c.deviceId, c.ClientId)

	go be_handle_command(ipc)

	var resp Ipc_packet
	select {
	case resp = <-response_chan:
		cmd_rsp := packet_cmd_response_unpack(resp.P.Data)

		if cmd_rsp.Cmd_status != CMD_STATUS_GOOD {
			logger_id(PRINT_FATAL, c.deviceId, "Test failed!, expected CMD_RESPONSE_GOOD got", cmd_rsp.Cmd_status)
		}

		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME * 100):
		logger_id(PRINT_FATAL, c.deviceId, "Timed out resetting device!")
	}

	site_mux_unreg_cmd(c.deviceId)
}

/*
func create_ipc_server_side_ack_stress_test(deviceId uint64, clientID uint64) Ipc_packet {
	cmd := Cmd_payload{}
	cmd.Cmd_payload = []byte("N/A")
	cmd.Cmd_type = ACK_STRESS_TEST

	ipc := Ipc_packet{}
	ipc.ClientId = clientID
	ipc.DeviceId = deviceId
	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED
	ipc.P.Data = packet_pack_cmd(cmd)

	return ipc
}*/
