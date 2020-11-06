package main

import (
	"os"
	"sync"

	"bitbucket.org/avd/go-ipc/mq"
)

var client_id_source uint64
var client_map map[uint64]chan Ipc_packet
var dmq_from_packet_to_core, dmq_from_core_to_packet *mq.LinuxMessageQueue
var client_id_mutex, mutex_map, mutex_mq sync.Mutex

func mq_write(msg []byte) error {
	mutex_mq.Lock()
	err := dmq_from_packet_to_core.Send(msg)
	if err != nil {
		logger(PRINT_FATAL, "Wrote to a dead IPC channel - OR IPC channel full -  should be very rare - restart EVERYTHING", err)
	}
	mutex_mq.Unlock()
	return err
}

func mq_closer() {
	dmq_from_packet_to_core.Destroy()
	logger(PRINT_WARN, "Closing MQ IPC")
}

func mq_listner() {
	init_map()
	init_lmq()
	defer mq_closer()

	for {
		rx := make([]byte, MAX_IPC_LEN) //PACKET_LEN_MAX)
		n, err := dmq_from_core_to_packet.Receive(rx)
		if err != nil {
			logger(PRINT_FATAL, "failed to read q")
		}

		ip := ipc_packet_unpack(rx)
		logger(PRINT_DEBUG, "ClientID: ", ip.ClientId, "Recieved packed_id, size : ", n)

		send_to_packet(ip)
	}
}

func register_client(id uint64, c chan Ipc_packet) {
	mutex_map.Lock()
	if c == nil {
		logger(PRINT_WARN, "ClientID: ", id, "failed, ipc_packet chan is nill")
		panic(0)
	}

	if _, ok := client_map[id]; ok {
		logger(PRINT_WARN, "ClientID: ", id, "failed, device id already registered")
	}
	client_map[id] = c
	mutex_map.Unlock()
}

func client_deregister(id uint64) {
	mutex_map.Lock()
	logger(PRINT_DEBUG, "CLIENT ID: ", id, "took mutex_map")
	// Close the Channel
	if _, ok := client_map[id]; ok {
		close(client_map[id])
		delete(client_map, id)
	} else {
		logger(PRINT_WARN, "CLIENT ID: ", id, "Tried to deregister a non-registered client")
	}
	mutex_map.Unlock()
	logger(PRINT_DEBUG, "CLIENT ID: ", id, "gave mutex_map")
}

func send_to_packet(ip Ipc_packet) {
	mutex_map.Lock()

	if _, ok := client_map[ip.ClientId]; !ok {
		logger(PRINT_WARN, "ClientID: ", ip.ClientId, "failed, client was not registered")
		mutex_map.Unlock()
		return
	}

	client_map[ip.ClientId] <- ip

	mutex_map.Unlock()
}

func init_map() {
	client_map = make(map[uint64]chan Ipc_packet)
}

func init_lmq() {
	var err error

	dmq_from_packet_to_core, err = mq.OpenLinuxMessageQueue("smq_from_packet_to_core", os.O_WRONLY)
	if err != nil {
		logger(PRINT_FATAL, "Could not create linux smq_client - (did yu increase the limit in /proc/sys/fs/mqueue/msg_max?  error: ", err)
	}

	dmq_from_core_to_packet, err = mq.OpenLinuxMessageQueue("smq_from_core_to_packet", os.O_RDONLY)
	if err != nil {
		logger(PRINT_FATAL, "Could not create linux smq_client - (did yu increase the limit in /proc/sys/fs/mqueue/msg_max?  error: ", err)
	}

	logger(PRINT_DEBUG, "Created linux IPC")
}

func get_client_id() uint64 {
	var ret uint64

	client_id_mutex.Lock()
	ret = client_id_source
	client_id_source++
	client_id_mutex.Unlock()

	return ret
}

func ipc_starter(cs *Client_state) {
	go ipc_reader(cs.client_id, cs)
	go ipc_writer(cs.client_id, cs)
}

// Implementation of ipc_writter and reader is
// naive, assumes a lot (chennels will never close....etc)
// tood: fix these?

func ipc_writer(id uint64, cs *Client_state) {
	defer func() {
		logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "ipc write wait group done")
		cs.wg_ipc.Done()
	}()

	logger(PRINT_DEBUG, "ClientID: ", cs.client_id, "starting IPC writer")
	var ip Ipc_packet
	ip.ClientId = id

	for {
		select {
		case ip.P = <-cs.tcp_to_icp_reader_chan:
			break
		case <-cs.ipc_read_shutdown:
			logger(PRINT_NORMAL, "ClientId: ", cs.client_id, "Shutting down read ipc goroutine")
			return
		}
		logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "ipc_writer sending to linuxQ")
		err := mq_write(ipc_packet_pack(ip))
		if err != nil {
			logger(PRINT_FATAL, "ClientID: ", cs.client_id, "Will not handle failure to write to IPC messaeg queue - shut everything down and restart")
		}
	}
}

func ipc_reader(id uint64, cs *Client_state) {
	defer func() {
		logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "ipc read wait group done")
		cs.wg_ipc.Done()
	}()

	logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "starting IPC read server")
	// Register this ipc_reader with the ipc_core, the core will sending
	// any IPC packets to this particular client id into this chan
	var ipc_packet Ipc_packet
	ipc_read_chan := make(chan Ipc_packet, MAX_OUTSTANDING_TRANSACTIONS)
	register_client(id, ipc_read_chan)

	for {
		select {
		case ipc_packet = <-ipc_read_chan:
			break
		case <-cs.ipc_write_shutdown:
			//only once call is required to derigster, will close ipc_read_chan
			logger(PRINT_NORMAL, "ClientId: ", cs.client_id, "Shutting down write ipc goroutine")
			client_deregister(id)
			return
		}

		logger(PRINT_DEBUG, "ClientID: ", cs.client_id, "IPC_READER read a packet")

		cs.ipc_to_tcp_writer_chan <- ipc_packet.P
	}
}
