package main

func create_ipc_cmd_packet(DeviceId uint64, Type uint8) Ipc_packet {
	cmd := Cmd_payload{}
	cmd.Cmd_payload = []byte("N/A")
	cmd.Cmd_type = Type

	ipc := Ipc_packet{}
	ipc.DeviceId = DeviceId
	ipc.P.Packet_type = CMD_PACKET

	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED

	ipc.P.Data = packet_pack_cmd(cmd)

	return ipc
}
