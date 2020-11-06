FROM espressif/idf:latest

WORKDIR /root

COPY . .

RUN ./build.sh /opt/esp/idf build

ENTRYPOINT ./build.sh /opt/esp/idf flash
