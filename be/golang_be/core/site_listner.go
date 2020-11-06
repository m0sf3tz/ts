package main

import (
	"log"
	"sync"
	"time"
)

var site_cmd_map_mutex sync.Mutex
var site_command_mux map[uint64]chan Ipc_packet

func test_cmd_response_mux(ip Ipc_packet) {
	logger(PRINT_SUPER_DEBUG, "Taking log site_cmd_map_mutex in test_cmd_response_mux")
	site_cmd_map_mutex.Lock()
	if _, ok := site_command_mux[ip.DeviceId]; !ok {
		if TEST_MODE {
			logger(PRINT_FATAL, "got a packet to an unregistered DeviceId: ", ip.DeviceId)
		} else {
			logger(PRINT_WARN, "got a packet to ann unregistered DeviceId: ", ip.DeviceId)
			site_cmd_map_mutex.Unlock()
			return
		}
	}
	c, ok := site_command_mux[ip.DeviceId]
	if ok {
		c <- ip
	}
	site_cmd_map_mutex.Unlock()
}

func site_mux_reg_cmd(DeviceId uint64, c chan Ipc_packet) {
	logger(PRINT_SUPER_DEBUG, "Taking Lock site_cmd_map_mutex in site_mux_reg_cmd")
	site_cmd_map_mutex.Lock()
	site_command_mux[DeviceId] = c
	site_cmd_map_mutex.Unlock()
}

func site_mux_unreg_cmd(DeviceId uint64) {
	logger(PRINT_SUPER_DEBUG, "Taking Lock site_cmd_map_mutex in site_mux_unreg_cmd")
	site_cmd_map_mutex.Lock()
	close(site_command_mux[DeviceId])
	delete(site_command_mux, DeviceId)
	site_cmd_map_mutex.Unlock()

	// work around for issue seen when one command starts before another is ended
	time.Sleep(time.Millisecond * 100)
}

func handle_incomming_ipc_site(ip Ipc_packet) {
	go test_cmd_response_mux(ip)
}

func mq_site_listner() {
	logger(PRINT_NORMAL, "Starting Site Listner")

	rx := make([]byte, PACKET_LEN_MAX)
	var n int
	var err error
	for {
		n, err = dmq_from_core_to_site.Receive(rx)
		if err != nil {
			log.Fatal("failed to read q") //TODO: not make fatal
		}

		ip := ipc_packet_unpack(rx[:n])
		handle_incomming_ipc_site(ip)
	}
}
