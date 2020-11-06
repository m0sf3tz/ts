rm constants.go ipc_constants.go crc.go logger.go packet_helper.go server_config.go
rm masterCore 

ln -s ../packet/constants.go constants.go 
ln -s ../packet/ipc_constants.go ipc_constants.go 
ln -s ../packet/logger.go logger.go
ln -s ../packet/packet_helper.go packet_helper.go  
ln -s ../packet/server_config.go server_config.go
ln -s ../packet/crc.go crc.go 

CORE_GO="masterCore.go exls.go rscript.go site_constants.go crc.go client_helper.go fota.go command_mux.go test_fota.go test_routines.go constants.go server_config.go packet_helper.go logger.go ipc_constants.go db.go ipc_helper.go core_constants.go file_helper.go file_constants.go site_listner.go ack_stress_test.go site.go site_helper.go" 

go build -race $SITE_GO $CORE_GO

FILES=$PWD/*
for f in $FILES 
do
  if [ -L $f ]; then
    chmod 0444 $f
  fi
done


if [ $? != 0 ]; then
  exit 
fi

if [ ! -z "$1" ]; then
  if [ "$1" == "-r" ]; then 
    ./masterCore
  fi
fi
