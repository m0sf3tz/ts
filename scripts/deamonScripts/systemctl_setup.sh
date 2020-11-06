sudo mkdir /usr/local/timeScan
sudo cp -r ../../be/golang_be/core   /usr/local/timeScan/core 
sudo cp -r ../../be/golang_be/packet /usr/local/timeScan/packet

sudo cp timeScan-Daemon.sh /usr/local/timeScan  
sudo cp timeScan.service  /etc/systemd/system

sudo systemctl enable timeScan.service
sudo systemctl start timeScan.service
