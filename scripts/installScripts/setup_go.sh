#!/bin/bash -i 
#above -i requried ot run in interactrive mode)

wget "https://dl.google.com/go/go1.14.4.linux-amd64.tar.gz" -O go.tar.gz
sudo tar -C /usr/local -xzf go.tar.gz

echo "export PATH=\$PATH:/usr/local/go/bin" >> ~/.bashrc
echo "export GOPATH=\"$HOME/\"" >> ~/.bashrc
echo "export GOBIN=\"$GOPATH/bin\"" >> ~/.bashrc

#must source, to call go (if not in interactive mode, would fail, hence #/!bin/bash -i
. ~/.bashrc

go get github.com/Pallinder/go-randomdata # Not raelly required, just for testing
go get github.com/tealeg/xlsx
go get github.com/gorilla/sessions
go get bitbucket.org/avd/go-ipc/mq
go get github.com/gorilla/websocket
go get github.com/lib/pq
