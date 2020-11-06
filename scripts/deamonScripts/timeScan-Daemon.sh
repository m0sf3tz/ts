export R_LIBS_USER=~/Rpackages

echo "Staring mastercore!"
cd /usr/local/timeScan/core 
echo $PWD
./masterCore &
echo "masterCore PID == $!"
MASTERCORE_PID=$!

sleep 2

echo "Staring server!"
cd /usr/local/timeScan/packet 
./server &
echo "server PID == $!"
SERVER_PID=$!

sleep 2

while [ true ]; do
  ps -p "$MASTERCORE_PID" > /dev/null 
  MASTERCORE_RUNNING=$?

  ps -p "$SERVER_PID" > /dev/null 
  SERVER_RUNNING=$?
  
  if [ $MASTERCORE_RUNNING -eq 0 ] && [ $SERVER_RUNNING -eq 0 ]; then 
    echo "process running!"
    sleep 2
  else 
    echo "mastercore running = $MASTERCORE_RUNNING"
    echo "server running = $SERVER_RUNNING"
    #one or both PID dead, kill both 
    kill $SERVER_PID 
    kill $MASTERCORE_PID 

    echo "proess stopped!"
    exit 1
  fi
done
