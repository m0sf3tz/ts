package main

import (
	"net"
	"runtime"
	"sync"
	"time"
)

type Client_state struct {
	init                   bool
	err_chan_tcp           chan bool
	tcp_write_shutdown     chan bool
	tcp_socket_writer_chan chan Packet
	tcp_socket_reader_chan chan Packet

	//transaction accountant
	m       map[uint16]map_packet
	m_mutex *sync.Mutex

	// Timer -> Client core
	client_event_timer      chan bool
	client_event_timer_kill chan bool

	// Unix -> Client core
	ipc_to_tcp_writer_chan chan Packet
	tcp_to_icp_reader_chan chan Packet
	ipc_read_shutdown      chan bool
	ipc_write_shutdown     chan bool
	ipc_shutdown           chan bool
	client_id              uint64

	wg_ipc    sync.WaitGroup
	wg_tcp    sync.WaitGroup
	wg_event  sync.WaitGroup
	device_id string
}

func waitTimeout(wg *sync.WaitGroup, timeout time.Duration) bool {
	c := make(chan struct{})
	go func() {
		defer close(c)
		wg.Wait()
	}()
	select {
	case <-c:
		return false // completed normally
	case <-time.After(timeout):
		return true // timed out
	}
}

// Handles incoming requests.
func Client_handler(conn net.Conn) {

	// Set up the client state
	cs := Client_state{}
	// Set up the timeouts
	//set_time_outs(&conn) //TODO: is this needed?

	// Initilize the packet_accountant
	init_transaction_accountant(&cs)

	// Must create the error channel we will share with the two TCP
	// reader and writter tasks. If any errors occur during read/write/
	// chunking the sub tasks will notify the client hanlder through
	// this error channel

	// the two TCP writer/reader slaves will
	// write into this channel to let client_handler
	// know something is wrong. The tcp_write_shutdown channel
	// is used to let the tcp_socket_writter goroutine to know
	// it is time to shutdown
	cs.err_chan_tcp = make(chan bool, 10) //ther are max 3 writters into this, tcp_write, tcp_read, tranaction_accountant, add some buffer just incase
	cs.tcp_write_shutdown = make(chan bool, 10)

	// Handles communication with tcp writer/reader tasks
	cs.tcp_socket_writer_chan = make(chan Packet, MAX_OUTSTANDING_TCP_CORE_SEND)
	cs.tcp_socket_reader_chan = make(chan Packet, MAX_OUTSTANDING_TCP_CORE_SEND)

	cs.client_event_timer = make(chan bool, 1)      // Internal to client handler
	cs.client_event_timer_kill = make(chan bool, 2) // Internal to client handler

	cs.ipc_to_tcp_writer_chan = make(chan Packet, MAX_OUTSTANDING_TCP_CORE_SEND) // IPC ---> TCP
	cs.tcp_to_icp_reader_chan = make(chan Packet, MAX_OUTSTANDING_TCP_CORE_SEND) // IPC <--- TCP

	cs.ipc_read_shutdown = make(chan bool, 10)  // IPC (internal)
	cs.ipc_write_shutdown = make(chan bool, 10) // IPC (internal)
	cs.ipc_shutdown = make(chan bool, 10)       // IPC (internal) can write to this to triger IPC shutdown and reset

	cs.client_id = get_client_id()

	// 1 for the writter task, 1 for the reader
	cs.wg_tcp.Add(2)

	//1 for the event generator
	cs.wg_event.Add(1)

	// 2 for the ipc writter reader task
	cs.wg_ipc.Add(2)

	go tcp_starter(conn, &cs)
	go ipc_starter(&cs)

	// Will trigger the event timer
	go event_generator(&cs)

	for {
		select {
		case <-cs.err_chan_tcp:
			logger(PRINT_WARN, "ClientID: ", cs.client_id, "A TCP error messge was recieved")
			cs.tcp_write_shutdown <- true
			cs.ipc_shutdown <- true
			logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "Sending suicide instruction to event goroutine")
			cs.client_event_timer_kill <- true
			break

		case <-cs.ipc_shutdown:
			logger(PRINT_WARN, "ClientID: ", cs.client_id, "shutting down IPC")
			cs.ipc_read_shutdown <- true
			cs.ipc_write_shutdown <- true
			goto shutdown_client

		case tcp_rx := <-cs.tcp_socket_reader_chan:
			logger(PRINT_DEBUG, "ClientID: ", cs.client_id, "sending to ipc output")
			client_dequeue_transaction(tcp_rx, &cs) // Handle acks (will not go to IPC, only NAKs or no responses)
			client_core_handle_packet_rx(tcp_rx, &cs)
			break

		case ipc_rx := <-cs.ipc_to_tcp_writer_chan:
			logger(PRINT_DEBUG, "ClientID: ", cs.client_id, " Sending to TCP")
			cs.tcp_socket_writer_chan <- ipc_rx
			client_enqueue_transaction(ipc_rx, &cs)
			break

		case <-cs.client_event_timer:
			transaction_scan_timeout(&cs)
			break
		}
	}

	// here we handle all todos related to shutting down a client
shutdown_client:
	logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "waiting for event goroutine to die")

	if waitTimeout(&(cs.wg_event), time.Second) {
		logger(PRINT_FATAL, "ClientID: ", cs.client_id, "Timed out waiting for wait group (event)")
	} else {
		logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "Finished waiting for event generator to close!")
	}
	close(cs.client_event_timer)

	if waitTimeout(&(cs.wg_tcp), time.Second*2) {
		logger(PRINT_FATAL, "ClientID: ", cs.client_id, "Timed out waiting for wait group (tcp)")
	} else {
		logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "Finished waiting for TCP writter/reader to close!")
	}

	// wait for the IPC to shut down
	if waitTimeout(&(cs.wg_ipc), time.Second*2) {
		logger(PRINT_FATAL, "ClientID: ", cs.client_id, "Timed out waiting for wait group (ipc)")
	} else {
		logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "Wait group finished")
	}

	logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "closing client_handler")
	close(cs.err_chan_tcp)
	close(cs.tcp_write_shutdown)

	close(cs.tcp_socket_reader_chan)
	close(cs.tcp_socket_writer_chan)

	close(cs.ipc_to_tcp_writer_chan)
	close(cs.tcp_to_icp_reader_chan)

	close(cs.ipc_read_shutdown)
	close(cs.ipc_write_shutdown)
	close(cs.ipc_shutdown)

	// Send a good bye packet to packet core
	good_bye := create_goodbye_packet(cs.client_id)
	err := mq_write(ipc_packet_pack(good_bye))
	if err != nil {
		logger(PRINT_FATAL, "ClientID: ", cs.client_id, "Will not handle failure to write to IPC messaeg queue - shut everything down and restart")
	}

	logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "Finished closing")
	logger(PRINT_DEBUG, "# of goroutines: ", runtime.NumGoroutine())
	time.Sleep(time.Second * 1)

}

// If a packet requires an ACK, we will track it here
func client_enqueue_transaction(p Packet, cs *Client_state) {
	if p.Consumer_ack_req != CONSUMER_ACK_REQUIRED {
		return
	}
	logger(PRINT_DEBUG, "ClientID: ", cs.client_id, "Transaction_id ", p.Transaction_id, " needs a device ACK, adding it to the TX map")
	transactions_append(p.Transaction_id, cs)
}

// If device-ack, handle it here
func client_dequeue_transaction(p Packet, cs *Client_state) {
	if p.Packet_type == DEVICE_ACK_PACKET {
		logger(PRINT_DEBUG, "ClientID: ", cs.client_id, "Transaction_id ", p.Transaction_id, " Was popped, removing it from the TX map")
		transactions_pop(p.Transaction_id, cs)
	}
}

func event_generator(cs *Client_state) {
	defer func() {
		logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "ipc event wait group done")
		cs.wg_event.Done()
	}()

	var counter int
	for {
		select {
		case <-cs.client_event_timer_kill:
			logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "event goroutine rxed suicide instruction, dying...")
			return
		default:
		}

		time.Sleep(time.Millisecond * 250)
		select {
		case cs.client_event_timer <- true:
		default:
			logger(PRINT_WARN, "ClientID: ", cs.client_id, "Was not able to write into cs.client_event_timer")
			counter++
			if counter == 10 {
				logger(PRINT_FATAL, "ClientID: ", cs.client_id, "got stuck trying to shut down event generator")
			}
		}
	}
}

// tcp core processed a packet, handle it
// this could be an..
// -> device ack
// -> login packet (goes to BE)
// -> query packet (goes to BE)
func client_core_handle_packet_rx(p Packet, cs *Client_state) {
	if p.Consumer_ack_req == CONSUMER_ACK_REQUIRED {
		logger(PRINT_DEBUG, "ClientID: ", cs.client_id, "Sending ACK to device for transaction_id", p.Transaction_id)
		cs.tcp_socket_writer_chan <- create_ack_pack(p, ACK_GOOD)
	}

	if p.Packet_type == DEVICE_ACK_PACKET {
		// device acks don't go to master-core
		return
	}

	cs.tcp_to_icp_reader_chan <- p
}
