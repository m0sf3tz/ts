sudo mkdir /usr/local/timeScan
sudo cp -r ../../be/golang_be/core   /usr/local/timeScan/core 
sudo cp -r ../../be/golang_be/packet /usr/local/timeScan/packet


sudo systemctl enable timeScan.service
