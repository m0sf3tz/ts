package main

import (
	"sync"
	"time"
)

type map_packet struct {
	transaction_id uint16
	timestamp      int64
}

func transactions_pop(transaction_id uint16, cs *Client_state) int {
	cs.m_mutex.Lock()
	// Check to see if a transaction_id is actually in the LL before
	_, ok := cs.m[transaction_id]

	if !ok {
		logger(PRINT_FATAL, "Popped a transaction_id %d that was already popped", transaction_id)
	}
	delete(cs.m, transaction_id)

	cs.m_mutex.Unlock()
	return int(transaction_id)
}

func transaction_scan_timeout(cs *Client_state) {
	cs.m_mutex.Lock()
	for k := range cs.m {
		if time_ms_since_timestamp(cs.m[k].timestamp) > TCP_PACKET_MS_NO_ACK_CONSIDERED_LOST {
			logger(PRINT_WARN, "Lost packet: ", cs.m[k].transaction_id)

			// create nack
			p := Packet{}
			p.Consumer_ack_req = 0
			p.Packet_type = CMD_RESPONSE_PACKET
			p.Transaction_id = cs.m[k].transaction_id

			payload := Cmd_resp_payload{}
			payload.Total_packets = 1
			payload.Cmd_status = CMD_ACK_TIMED_OUT
			payload.Resp_payload = make([]byte, CMD_RESPONSE_PAYLOAD_LEN)
			p.Data = cmd_payload_pack(payload)

			// send it to the client core
			cs.tcp_to_icp_reader_chan <- p

			// close this connection
			logger(PRINT_WARN, "Transaction Accountant triggering TCP disconnect")
			cs.err_chan_tcp <- true

			// pop from map
			delete(cs.m, cs.m[k].transaction_id)
		}
	}
	cs.m_mutex.Unlock()
}

func transactions_append(transaction_id uint16, cs *Client_state) {
	cs.m_mutex.Lock()
	// Check if the key is already in the map
	_, ok := cs.m[transaction_id]

	if ok {
		logger(PRINT_FATAL, "FATAL ERROR: Key already preset when trying to add transaction_id:", transaction_id)
	}

	if len(cs.m) >= MAX_OUTSTANDING_TRANSACTIONS {
		logger(PRINT_FATAL, "FATAL ERROR: Outstanding transactions full, unexpected when inserting:", transaction_id)
	}

	cs.m[transaction_id] = map_packet{transaction_id: transaction_id, timestamp: time_ms_since_epoch()}
	cs.m_mutex.Unlock()
}

func time_ms_since_epoch() int64 {
	return time.Now().UnixNano() / 1e6
}

func time_ms_since_timestamp(timestamp int64) int64 {
	timenow := time.Now().UnixNano() / 1e6
	ret := timenow - timestamp
	if ret < 0 {
		logger(PRINT_FATAL, "Negative timestamp - not sure what happened!")
	}
	return ret
}

func init_transaction_accountant(cs *Client_state) {
	cs.m = make(map[uint16]map_packet)
	cs.m_mutex = &sync.Mutex{}
}
