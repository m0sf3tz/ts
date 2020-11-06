package main

/**********************************************************
*       Used to define IPC message queue properiets
*********************************************************/

const IPC_QUEUE_DEPTH = (5)
const IPC_QUEUE_SIZE = (PACKET_LEN_MAX)
const IPC_QUEUE_PERM = (0666)

// +16 comes form the size of the two uint64s
const MAX_IPC_LEN = (PACKET_LEN_MAX + 16)

type Ipc_packet struct {
	P        Packet
	ClientId uint64
	DeviceId uint64
	token    uint32
}
