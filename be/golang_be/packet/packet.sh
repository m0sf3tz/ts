go build -race server.go   client_core.go   \
                           tcp_core.go constants.go     \
                           ipc_core.go packet_helper.go \
                           server_config.go logger.go   \
                           transaction_accountant.go    \
                           ipc_constants.go crc.go 

if [ $? != 0 ]; then
  exit 
fi

if [ ! -z "$1" ]; then
  if [ "$1" == "-r" ]; then 
    ./server
  fi
fi
