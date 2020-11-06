#!/bin/bash

cd installScripts

./setup_go.sh
./psql_init.sh
./systemctl_setup.sh 

sudo -u postgres psql -f ./setup.sql 

cd .. 
cd deamonScripts 

#set time to vancouver
sudo timedatectl set-timezone America/Los_Angeles

./systemctl_setup.sh 
