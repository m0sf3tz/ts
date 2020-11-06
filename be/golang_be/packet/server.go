package main

import (
	"fmt"
	"net"
	"time"
)

// This is very important, it will prevent stale connections
// from clogging up the server and hogging server cycles/files
func set_time_outs(conn *net.TCPConn) error {

	err1 := (*conn).SetKeepAlive(true)
	err2 := (*conn).SetKeepAlivePeriod(time.Second * 10)

	if err1 != nil {
		logger(PRINT_FATAL, "Could not set timeout!", err1)
	}
	if err2 != nil {
		logger(PRINT_FATAL, "Could not set timeout!", err2)
	}
	return nil
}

func timeout(t time.Duration, timeout chan bool) {
	time.Sleep(t)
	timeout <- true
}

func main() {
	logger_init("_packet")

	// Start the IPC listner, TCP clients will hook into the IPC core
	// to communicate to the backend
	go mq_listner()

	for {
		addr, err := net.ResolveTCPAddr("tcp", CONN_HOST+":"+CONN_PORT)
		if err != nil {
			panic(err)
		}

		l, err := net.ListenTCP(CONN_TYPE, addr)
		if err != nil {
			fmt.Println("Error listening:", err.Error())
			/* strange... retry in a bit */
			time.Sleep(time.Second * 5)
			continue
		}

		// Close the listener when the application closes.
		defer l.Close()
		fmt.Println("Listening on " + CONN_HOST + ":" + CONN_PORT)
		for {
			// Listen for an incoming connection.
			conn, err := l.AcceptTCP()
			set_time_outs(conn)

			if err != nil {
				fmt.Println("Error accepting: ", err.Error())
				/* retry timer */
				time.Sleep(time.Second * 5)
				break
			} else {
				go Client_handler(conn)
				logger(PRINT_NORMAL, "ClientID: N/A New client attached") //TODO: print client TCP
			}
		}
	}
}
