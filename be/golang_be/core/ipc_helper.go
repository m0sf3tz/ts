package main

func get_packet_sequence_number(ipc Ipc_packet) uint8 {
	cmd_rsp := packet_cmd_response_unpack(ipc.P.Data)
	return cmd_rsp.Packets_sequence_number
}

func get_total_reponse_packets(ipc Ipc_packet) uint8 {
	cmd_rsp := packet_cmd_response_unpack(ipc.P.Data)
	return cmd_rsp.Total_packets
}

func create_fail_packet(ipc Ipc_packet, reason uint8) Ipc_packet {
	fail_packet := Ipc_packet{}
	fail_packet.ClientId = ipc.ClientId
	fail_packet.DeviceId = ipc.DeviceId
	fail_packet.P.Packet_type = CMD_RESPONSE_PACKET

	payload := Cmd_resp_payload{}
	payload.Cmd_status = reason
	payload.Transaction_id = 0
	payload.Payload_len = 0
	payload.Resp_payload = make([]byte, CMD_RESPONSE_PAYLOAD_LEN)

	fail_packet.P.Data = cmd_payload_pack(payload)
	return fail_packet
}
