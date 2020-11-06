package main

import (
	"time"
)

func get_outstanding_command_or_fota_for_device(DeviceId uint64) bool {
	logger(PRINT_SUPER_DEBUG, "Taking lock device_busy_mutex in get_outstanding_command_or_fota_for_device")
	device_busy_mutex.Lock()
	ret := false
	if _, ok := device_busy[DeviceId]; ok {
		ret = true
	}
	device_busy_mutex.Unlock()
	return ret
}

func set_outstanding_command_or_fota_for_device(DeviceId uint64, operation bool) {
	logger(PRINT_SUPER_DEBUG, "Taking lock device_busy_mutex in set_outstanding_command_or_fota_for_device")
	device_busy_mutex.Lock()
	if operation {
		device_busy[DeviceId] = true
	} else {
		delete(device_busy, DeviceId)
	}
	device_busy_mutex.Unlock()
}

func check_device_active(DeviceId uint64) (bool, uint64) {
	logger(PRINT_SUPER_DEBUG, "Taking lock client_map_mutext in check_device_active")
	client_map_mutext.Lock()
	ret := true
	client := uint64(0)
	if _, ok := client_map[DeviceId]; !ok {
		client_map_mutext.Unlock()
		return false, 0
	}
	client = client_map[DeviceId].ClientId
	client_map_mutext.Unlock()
	return ret, client
}

// This muxes incomming packets from the "packet core"
// to individual command listners (ie, goroutines of be_handle_command)
// maps device_id to a channel
func register_command_mux(ipc Ipc_packet, c chan Ipc_packet) {
	// first get the clientId of this device.
	// PacketCore deals w/ "clientIds" (they are assigned on a first come first serve basis"
	// HTTP core deals with "DeviceIds", these are hardcoded into the device NV
	// we must translate between the two - the following code does this
	logger(PRINT_SUPER_DEBUG, "Taking lock client_map_mutext in register_command_mux")
	client_map_mutext.Lock()
	clientId := client_map[ipc.DeviceId].ClientId

	//cmd_mux mutex
	logger(PRINT_SUPER_DEBUG, "Taking lock cmd_mux_mutex in register_command_mux")
	cmd_mux_mutex.Lock()
	cmd_mux[clientId] = c
	cmd_mux_mutex.Unlock()

	client_map_mutext.Unlock()
}

func deregister_command_mux(ipc Ipc_packet) {
	logger(PRINT_SUPER_DEBUG, "Taking lock client_map_mutext in deregister_command_mux")
	client_map_mutext.Lock()
	clientId := client_map[ipc.DeviceId].ClientId

	logger(PRINT_SUPER_DEBUG, "Taking lock cmd_mux_mutex in deregister_command_mux")
	cmd_mux_mutex.Lock()
	logger(PRINT_NORMAL, "Closnig on clientId == ", clientId)
	close(cmd_mux[clientId])
	delete(cmd_mux, clientId)
	cmd_mux_mutex.Unlock()

	client_map_mutext.Unlock()
}

func be_handle_command(ipc Ipc_packet) {
	var fail_packet Ipc_packet

	ok, clientId := check_device_active(ipc.DeviceId)
	ipc.ClientId = clientId

	logger_id(PRINT_NORMAL, ipc.DeviceId, "Sending to  ClientId: ", clientId)

	if !ok {
		logger_id(PRINT_WARN, ipc.DeviceId, "Tried to send a command to a dead client")
		fail_packet = create_fail_packet(ipc, SENT_TO_INACTIVE_CLIENT)
		dmq_from_core_to_site.Send(ipc_packet_pack(fail_packet))
		return
	}

	if get_outstanding_command_or_fota_for_device(ipc.DeviceId) {
		logger_id(PRINT_WARN, ipc.DeviceId, "Tried to send send two commands to one clientId")
		fail_packet = create_fail_packet(ipc, TOO_MANY_OUTSTANDING_COMMANDS)
		dmq_from_core_to_site.Send(ipc_packet_pack(fail_packet))
		return
	}

	// First, register with the command mux
	response_chan := make(chan Ipc_packet)
	register_command_mux(ipc, response_chan)

	//Send the command out
	dmq_from_core_to_packet.Send(ipc_packet_pack(ipc))

	resp_arr := make(map[int]Ipc_packet)
	counter := 0

	var time_out time.Duration
	if ipc.P.Data[0] == ACK_STRESS_TEST || ipc.P.Data[0] == SYNC_CMD {
		logger_id(PRINT_WARN, ipc.DeviceId, "Starting ACK stress test or SYNC_COMMAND, setting timeout to (extended)!")
		time_out = COMMAND_TIMEOUT_TIME_EXTENDED
	} else {
		time_out = COMMAND_TIMEOUT_TIME
	}

rx_loop:
	for {
		select {
		case resp := <-response_chan:
			if resp.token == 0 {
				logger_id(PRINT_WARN, ipc.DeviceId, "Read from a closed channel, probably crashed")
				set_outstanding_command_or_fota_for_device(ipc.DeviceId, false)
				fail_packet := create_fail_packet(ipc, 51)
				dmq_from_core_to_site.Send(ipc_packet_pack(fail_packet))
				return
			}

			resp.DeviceId = ipc.DeviceId
			resp_arr[int(get_packet_sequence_number(resp))] = resp

			counter++

			if counter == int(get_total_reponse_packets(resp)) {
				break rx_loop
			}
		case <-time.After(time.Second * time_out):
			logger_id(PRINT_WARN, ipc.DeviceId, "Command timed out! (from command mux!), timeout == ", time_out)
			fail_packet := create_fail_packet(ipc, CMD_TIME_OUT)
			deregister_command_mux(ipc)
			set_outstanding_command_or_fota_for_device(ipc.DeviceId, false)
			dmq_from_core_to_site.Send(ipc_packet_pack(fail_packet))
			return
		}
	}

	// For multi-part responses, will iterate and send each response back
	for _, v := range resp_arr {
		dmq_from_core_to_site.Send(ipc_packet_pack(v))
	}

	logger_id(PRINT_NORMAL, ipc.DeviceId, "Deregestring from command mux!")
	deregister_command_mux(ipc)

	set_outstanding_command_or_fota_for_device(ipc.DeviceId, false)
}

func cmd_response_mux(ip Ipc_packet) {
	logger(PRINT_SUPER_DEBUG, "Taking lock cmd_mux_mutex in cmd_response_mux")
	cmd_mux_mutex.Lock()
	channel, ok := cmd_mux[ip.ClientId]
	cmd_mux_mutex.Unlock()

	if channel == nil {
		logger(PRINT_WARN, "DeviceId: ", ip.DeviceId, " - cmd_rsp_mux tried to send to null channel")
	}

	// incase the channel is closed by the time we write into it, silently drop it.. this is "bad" progarming - but so far it works
	defer func() {
		if err := recover(); err != nil {
			logger(PRINT_WARN, "DeviceId: ", ip.DeviceId, " - cmd_rsp_mux tried to write to a closed channel, err = ", err)
			logger(PRINT_NORMAL, err)
		}
	}()

	if ok {
		/* set the token, so if we do a read we know if we read a on a closed channel */
		ip.token = 0xdeadbeef
		channel <- ip
	}
}
