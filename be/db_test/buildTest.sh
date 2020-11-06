rm exls.go db.go logger.go site_constants.go server_config.go constants.go core_constants.go 
rm test  

ln -s ../golang_be/core/db.go db.go  
ln -s ../golang_be/core/site_constants.go site_constants.go
ln -s ../golang_be/core/core_constants.go core_constants.go
ln -s ../golang_be/core/exls.go exls.go

ln -s ../golang_be/packet/logger.go logger.go  
ln -s ../golang_be/packet/server_config.go server_config.go  
ln -s ../golang_be/packet/constants.go constants.go  

TEST_DB="test.go exls.go db.go logger.go site_constants.go server_config.go constants.go core_constants.go"

go build $TEST_DB 

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
