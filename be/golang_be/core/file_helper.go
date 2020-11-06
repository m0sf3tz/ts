package main

import (
	_ "sort"
	"time"
)

func send_sycn_packet(c client, bit_field []byte, mode uint8) uint8 {
	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)

	ipc := create_ipc_sync_packet(c.deviceId, bit_field, mode)
	ipc.ClientId = c.ClientId

	go be_handle_command(ipc)

	select {
	case resp := <-response_chan:
		cmd_rsp := packet_cmd_response_unpack(resp.P.Data)
		site_mux_unreg_cmd(c.deviceId)
		return uint8(cmd_rsp.Cmd_status)
		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME_EXTENDED):
		site_mux_unreg_cmd(c.deviceId)
		logger_id(PRINT_WARN, c.deviceId, "Timed out during SYNC command")
	}
	/* should not get here*/
	return 0
}

func test_add_user(c client, name string, expected_response uint8, id uint32, replace bool) {
	logger_id(PRINT_WARN, c.deviceId, "Starting to send test_add_user command")

	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)

	ipc := create_ipc_cmd_add_usr_to_flash(c.deviceId, name, id, replace)
	ipc.ClientId = c.ClientId

	go be_handle_command(ipc)

	select {
	case resp := <-response_chan:
		cmd_rsp := packet_cmd_response_unpack(resp.P.Data)

		if cmd_rsp.Cmd_status != expected_response {
			site_mux_unreg_cmd(c.deviceId)
			if TEST_MODE {
				logger(PRINT_NORMAL, cmd_rsp)
				logger_id(PRINT_FATAL, c.deviceId, "Test failed!, expected", expected_response, "got", cmd_rsp.Cmd_status)
			}
		}
		site_mux_unreg_cmd(c.deviceId)
		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME):
		site_mux_unreg_cmd(c.deviceId)
		logger_id(PRINT_FATAL, c.deviceId, "Timed out adding a user")
	}
	logger_id(PRINT_NORMAL, c.deviceId, "Done adding a user")
}

func test_delete_user(c client, expected_response uint8, slotId uint16) {
	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)

	ipc := create_ipc_cmd_dlt_usr_helper(c.deviceId, slotId, false)
	ipc.ClientId = c.ClientId

	go be_handle_command(ipc)

	select {
	case resp := <-response_chan:
		cmd_rsp := packet_cmd_response_unpack(resp.P.Data)

		if cmd_rsp.Cmd_status != expected_response {
			site_mux_unreg_cmd(c.deviceId)
			if TEST_MODE {
				logger(PRINT_NORMAL, cmd_rsp)
				logger_id(PRINT_FATAL, c.deviceId, "Test failed!, expected", expected_response, "got", cmd_rsp.Cmd_status)
			}
		}
		site_mux_unreg_cmd(c.deviceId)
		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME):
		site_mux_unreg_cmd(c.deviceId)
		logger_id(PRINT_FATAL, c.deviceId, "Timed out deleting user")
	}
	logger_id(PRINT_NORMAL, c.deviceId, "Done deleting user")
}

func delete_all_users(c client) bool {
	logger_id(PRINT_NORMAL, c.deviceId, "Starting to delete all users")

	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)
	ipc := create_ipc_cmd_packet(c.deviceId, DELETE_ALL_USERS_CMD)
	ipc.ClientId = c.ClientId
	ipc.DeviceId = c.deviceId

	go be_handle_command(ipc)

	select {
	case resp := <-response_chan:
		cmd_rsp := packet_cmd_response_unpack(resp.P.Data)
		if cmd_rsp.Cmd_status != CMD_STATUS_GOOD {
			if TEST_MODE {
				logger_id(PRINT_FATAL, c.deviceId, "Unexpected NAK:", cmd_rsp.Cmd_status)
			} else {
				logger_id(PRINT_WARN, c.deviceId, "Unexpected NAK:", cmd_rsp.Cmd_status)
			}
		}
		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME):
		if TEST_MODE {
			logger_id(PRINT_FATAL, c.deviceId, "Timed out deleting all users")
		} else {
			site_mux_unreg_cmd(c.deviceId)
			return false
		}
	}
	logger_id(PRINT_NORMAL, c.deviceId, "Done deleting all users")
	site_mux_unreg_cmd(c.deviceId)
	return true
}

func get_all_users(c client, sync bool) (bool, []employee, uint8) {
	logger_id(PRINT_NORMAL, c.deviceId, "Fetcing all users")
	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)

	var ipc Ipc_packet
	if sync {
		ipc = create_ipc_cmd_packet(c.deviceId, GET_ALL_USERS_PRE_SYNC)
	} else {
		ipc = create_ipc_cmd_packet(c.deviceId, GET_ALL_USERS_CMD)
	}
	ipc.ClientId = c.ClientId

	ret := make([]employee, 0, MAX_USERS_DEVICE)
	counter := 0
	resp_arr := make(map[int]Ipc_packet)

	go be_handle_command(ipc)

	for {
		select {
		case resp := <-response_chan:

			resp_arr[int(get_packet_sequence_number(resp))] = resp
			counter++

			if counter == int(get_total_reponse_packets(resp)) {
				logger(PRINT_NORMAL, "Finished geting all packets of a get_all_users_cmd")
				goto check
			}
		case <-time.After(time.Second * CMD_TIME_OUT):
			if TEST_MODE {
				logger(PRINT_FATAL, "Timed out getting all users")
			} else {
				logger(PRINT_NORMAL, "Getting named packed out")
				site_mux_unreg_cmd(c.deviceId)
				return false, nil, CMD_TIMED_OUT_INTERNAL
			}
		}
	}
check:
	for i, v := range resp_arr {
		cmd_rsp := packet_cmd_response_unpack(v.P.Data)
		if cmd_rsp.Cmd_status != CMD_STATUS_GOOD {
			site_mux_unreg_cmd(c.deviceId)
			return true, ret, cmd_rsp.Cmd_status
		}

		rsp := packet_name_response_unpack((resp_arr[i]).P.Data)
		e := employee{}
		e.id = rsp.Internal_id
		e.name = string(rsp.Name)
		e.uid = rsp.Uid
		e.valid = true

		ret = append(ret, e)
		if TEST_MODE {
			logger(PRINT_NORMAL, e)
		}
	}
	logger_id(PRINT_NORMAL, c.deviceId, "Done Fetcing all users")
	site_mux_unreg_cmd(c.deviceId)
	return true, ret, CMD_STATUS_GOOD
}

func db_sync(c client, mode uint8) bool {
	valid, emp, _ := get_all_users(c, true)
	if !valid {
		logger(PRINT_WARN, "Could not get list of users, did client detach?")
		return false
	}
	_, bit_field := db_sync_users(emp)
	logger(PRINT_NORMAL, bit_field)
	send_sycn_packet(c, bit_field, mode)
	return true
}

/* Id is the internal ID that the device used, we get this from reading all the users first */
func delete_specific_user(c client, id uint16, delete_thumb bool) uint8 {
	logger_id(PRINT_NORMAL, c.deviceId, "deleting user id = ", id)
	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)

	var ipc Ipc_packet

	if delete_thumb {
		ipc = create_ipc_cmd_dlt_usr_helper(c.deviceId, id, true)
	} else {
		ipc = create_ipc_cmd_dlt_usr_helper(c.deviceId, id, false)
	}
	ipc.ClientId = c.ClientId
	var resp Ipc_packet

	go be_handle_command(ipc)

	select {
	case resp = <-response_chan:
		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME):
		site_mux_unreg_cmd(c.deviceId)
		logger(PRINT_FATAL, "Timed out delting a user")
	}

	site_mux_unreg_cmd(c.deviceId)

	cmd_rsp := packet_cmd_response_unpack(resp.P.Data)
	return cmd_rsp.Cmd_status
}

/* real comman, adds user to flash AND thumbring */
func add_user_to_flash_and_print(c client, name string, id uint32, replace bool) uint8 {
	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)

	ipc := create_ipc_cmd_add_usr(c.deviceId, name, id, replace)
	ipc.ClientId = c.ClientId

	go be_handle_command(ipc)

	cmd_rsp := Cmd_resp_payload{}

	select {
	case resp := <-response_chan:
		cmd_rsp = packet_cmd_response_unpack(resp.P.Data)
		site_mux_unreg_cmd(c.deviceId)
		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME):
		site_mux_unreg_cmd(c.deviceId)
		logger_id(PRINT_WARN, c.deviceId, "Timed out adding a user")
		cmd_rsp.Cmd_status = CMD_STATUS_FAILED
	}
	return cmd_rsp.Cmd_status
}

/* does NOT add a user to the fingerprint list, JUST FLASH ONLY */
func create_ipc_cmd_add_usr_to_flash(DeviceId uint64, name string, id uint32, replace bool) Ipc_packet {
	cmd := Cmd_payload{}
	cmd.Cmd_payload = create_add_user_payload(name, id, replace)
	cmd.Cmd_type = ADD_USER_CMD_TO_FLASH

	ipc := Ipc_packet{}
	ipc.DeviceId = DeviceId
	ipc.P.Packet_type = CMD_PACKET

	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED

	ipc.P.Data = packet_pack_cmd(cmd)

	return ipc
}

/* does NOT delete a user from fingerprint list, JUST FLASH ONLY */
func create_ipc_cmd_dlt_usr_helper(DeviceId uint64, id uint16, delete_print bool) Ipc_packet {
	cmd := Cmd_payload{}
	if id > MAX_USERS_DEVICE {
		logger(PRINT_FATAL, "Tried to delete a user that's larger then MAX_USERS_DEVICE")
	}

	cmd.Cmd_payload = make([]byte, 2)
	cmd.Cmd_payload[0] = byte(id)
	cmd.Cmd_payload[1] = byte((id >> 8) & 0xFF)
	if delete_print == true {
		cmd.Cmd_type = DELETE_SPECIFIC_USER_CMD
	} else {
		cmd.Cmd_type = DELETE_USER_FROM_FLASH_TEST
	}

	ipc := Ipc_packet{}
	ipc.DeviceId = DeviceId
	ipc.P.Packet_type = CMD_PACKET

	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED

	ipc.P.Data = packet_pack_cmd(cmd)

	return ipc
}

func create_ipc_cmd_add_usr(DeviceId uint64, name string, id uint32, replace bool) Ipc_packet {
	cmd := Cmd_payload{}
	cmd.Cmd_payload = create_add_user_payload(name, id, replace)
	cmd.Cmd_type = PUSH_USER_CMD

	ipc := Ipc_packet{}
	ipc.DeviceId = DeviceId
	ipc.P.Packet_type = CMD_PACKET

	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED

	ipc.P.Data = packet_pack_cmd(cmd)

	return ipc
}

func create_ipc_sync_packet(DeviceId uint64, bit_field []byte, mode uint8) Ipc_packet {
	cmd := Cmd_payload{}
	cmd.Cmd_payload = create_sync_payload(bit_field, MAX_USERS_DEVICE, mode)

	logger(PRINT_NORMAL, cmd.Cmd_payload)
	cmd.Cmd_type = SYNC_CMD

	ipc := Ipc_packet{}
	ipc.DeviceId = DeviceId
	ipc.P.Packet_type = CMD_PACKET

	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED

	ipc.P.Data = packet_pack_cmd(cmd)

	return ipc
}
