!/bin/bash

sudo apt-get install r-base -y
sudo apt-get install libcurl4-openssl-dev -y
sudo apt-get install libmagick++-dev -y 
sudo apt-get install libssl-dev -y
sudo apt-get install libpq-dev -y 
mkdir ~/Rpackages
Rscript r_install_script.R

echo "export R_LIBS_USER=~/Rpackages" >> ~/.bashrc

sudo -u postgres psql -f setup_r_test.sql
