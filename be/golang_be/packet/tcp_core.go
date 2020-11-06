package main

import (
	"io"
	"net"
)

//import "time"

type chunker_state struct {
	packet_parsed_so_far int
	pckt_size            int
	current_parse_type   int
	packet_wip           []byte
}

func chunker_reset(state *chunker_state) {
	state.packet_parsed_so_far = 0
	state.packet_wip = nil
	state.pckt_size = 0
	state.current_parse_type = -1
}

func chunker(state *chunker_state, rx []byte, lenght int, cs *Client_state) int {
	if len(rx) == 0 || lenght == 0 {
		return -1
	}

	var rx_processed = 0
	for rx_processed != lenght {

		// this function is "stateless", state is passed to it from tcp_socket_read(..)
		if state.current_parse_type == -1 {
			// the type is always the first byte in any packet
			// - also, it must be offset to the boundary of the next
			// incomming packet
			state.current_parse_type = int(rx[rx_processed])

			state.pckt_size = get_packet_len(uint8(state.current_parse_type))
		}

		var rx_left = lenght - rx_processed
		read_len := 0

		if rx_left <= (state.pckt_size - state.packet_parsed_so_far) {
			read_len = rx_left
		} else {
			read_len = state.pckt_size - state.packet_parsed_so_far
		}

		state.packet_wip = append(state.packet_wip, rx[rx_processed:rx_processed+read_len]...)
		state.packet_parsed_so_far += read_len
		rx_processed += read_len

		if state.packet_parsed_so_far == state.pckt_size {

			logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "Recieved a packet")
			rx_packet := packet_unpack(state.packet_wip[:state.pckt_size])
			// Let the client handler know we got a packet
			cs.tcp_socket_reader_chan <- rx_packet
			//reset internal structures
			chunker_reset(state)
		}
	}
	return 0
}

// Owns low level TCP reads
func tcp_socket_read(conn net.Conn, cs *Client_state) {
	defer func() {
		logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "read wait group done")
		cs.wg_tcp.Done()
	}()

	// Every connection needs a unique chunker
	// create a zero initilzied state that will be passed to each chunker, this
	// way every connection will get it's own "static" variables
	chunker_state := chunker_state{}
	chunker_reset(&chunker_state)

	chunk := make([]byte, PACKET_LEN_MAX)

	logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "starting TCP read server")
	for {
		n, err := conn.Read(chunk)

		if err != nil {
			if err != io.EOF {
				logger(PRINT_WARN, "ClientID: ", cs.client_id, "Got the following error on read %s", err)
			} else {
				logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "Connection closed by client")
			}
			cs.err_chan_tcp <- true
			goto err
			return
		}
		logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "chunking", n, " bytes")
		chunker(&chunker_state, chunk[0:n], n, cs)
	}
err:
	chunker_reset(&chunker_state)
	logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "Closing TCP read socket!")
	conn.Close()
	cs.err_chan_tcp <- true
}

// Owns low level TCP writes
func tcp_socket_write(conn net.Conn, cs *Client_state) {
	var packet Packet

	defer func() {
		logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "write wait group done")
		cs.wg_tcp.Done()
	}()

	for {
		select {
		case packet = <-cs.tcp_socket_writer_chan:
			break
		case <-cs.tcp_write_shutdown:
			goto err
		}

		packet_binary := packet_pack(packet)

		written := 0
		l := len(packet_binary)
		for written != l {
			n, err := conn.Write(packet_binary[written:])
			if err != nil {
				logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "got the following error while writting: ", err)
				goto err
			}
			written = written + n
			logger(PRINT_NORMAL, "ClientID:", cs.client_id, "wrote ", n, " bytes")
		}
	}

err:
	logger(PRINT_NORMAL, "ClientID: ", cs.client_id, "Closing TCP write socket!")
	conn.Close()
	cs.err_chan_tcp <- true
}

// Start the listener and writter goroutines
func tcp_starter(conn net.Conn, cs *Client_state) {
	go tcp_socket_read(conn, cs)
	go tcp_socket_write(conn, cs)
}
