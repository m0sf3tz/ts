package main

import (
	"bitbucket.org/avd/go-ipc/mq"
	"log"
	"math/rand"
	"os"
	"strconv"
	"sync"
	"time"
)

var TEST_MODE bool = true

var client_map map[uint64]client       // deviceId -> clientId
var client_map_i map[uint64]uint64     // clientId -> deviceId
var cmd_mux map[uint64]chan Ipc_packet // ClientId -> cmd_listern_channels
var device_busy map[uint64]bool        // internal, set to true if outstanding command from a client;
var cool_down_map map[uint32]time.Time // maps id to the time they signed up. It's used as a cool time timer.
//	 																		  So if someone logs in a few seconds after intially being added to the system they are not added to the databse

var device_busy_mutex, client_id_to_device_id_map, client_map_mutext, test_client_map_mutext, cmd_mux_mutex, cool_down_timer_mutex, sync_device_mutex sync.Mutex

var dmq_from_site_to_core, dmq_from_core_to_site *mq.LinuxMessageQueue
var dmq_from_packet_to_core, dmq_from_core_to_packet *mq.LinuxMessageQueue

func init_lmq_core() {
	mq.DestroyLinuxMessageQueue("smq_from_site_to_core")
	mq.DestroyLinuxMessageQueue("smq_from_core_to_site")
	mq.DestroyLinuxMessageQueue("smq_from_packet_to_core")
	mq.DestroyLinuxMessageQueue("smq_from_core_to_packet")

	var err error

	dmq_from_site_to_core, err = mq.CreateLinuxMessageQueue("smq_from_site_to_core", os.O_RDWR, IPC_QUEUE_PERM, 10, MAX_IPC_LEN)
	if err != nil {
		logger(PRINT_FATAL, "Could not create linux smq_client - (did yu increase the limit in /proc/sys/fs/mqueue/msg_max?  error: ", err)
	}

	dmq_from_core_to_site, err = mq.CreateLinuxMessageQueue("smq_from_core_to_site", os.O_RDWR, IPC_QUEUE_PERM, 10, MAX_IPC_LEN)
	if err != nil {
		logger(PRINT_FATAL, "Could not create linux smq_client - (did yu increase the limit in /proc/sys/fs/mqueue/msg_max?  error: ", err)
	}

	dmq_from_packet_to_core, err = mq.CreateLinuxMessageQueue("smq_from_packet_to_core", os.O_RDWR, IPC_QUEUE_PERM, 10, MAX_IPC_LEN)
	if err != nil {
		logger(PRINT_FATAL, "Could not create linux smq_client - (did yu increase the limit in /proc/sys/fs/mqueue/msg_max?  error: ", err)
	}

	dmq_from_core_to_packet, err = mq.CreateLinuxMessageQueue("smq_from_core_to_packet", os.O_RDWR, IPC_QUEUE_PERM, 10, MAX_IPC_LEN)
	if err != nil {
		logger(PRINT_FATAL, "Could not create linux smq_client - (did yu increase the limit in /proc/sys/fs/mqueue/msg_max?  error: ", err)
	}

	logger(PRINT_DEBUG, "Created linux IPC")
}

func check_device_bricked(DeviceId uint64) uint8 {
	var active bool
	var ret uint8

	logger(PRINT_SUPER_DEBUG, "Taking log client_map_mutex in check_device_bricked")

	client_map_mutext.Lock()
	if _, ok := client_map[DeviceId]; !ok {
		active = false
	} else {
		active = true
	}

	if active {
		ret = client_map[DeviceId].bricked
	}

	client_map_mutext.Unlock()
	return ret
}

func check_active_devices(DeviceId uint64) bool {
	var ret bool
	logger(PRINT_SUPER_DEBUG, "Taking log client_map_mutex in check_active_devices")
	client_map_mutext.Lock()
	if _, ok := client_map[DeviceId]; !ok {
		ret = false
	} else {
		ret = true
	}
	client_map_mutext.Unlock()
	return ret
}

/* this function is retareted, but we are doing this since I didn't capitilize client members and i'm too lazy to refactor (it will fail json marshal) */
func get_all_devices() []deviceSite {
	var ret []deviceSite
	var single deviceSite

	logger(PRINT_SUPER_DEBUG, "Taking lock client_map_mutext in get_all_devices")
	client_map_mutext.Lock()
	for _, v := range client_map {
		single.Device_name = v.device_name
		single.DeviceId = v.deviceId
		single.Fw_version = v.fw_version
		single.Bricked = v.bricked
		single.UpTime = time.Now().Sub(v.attach_time).String()
		ret = append(ret, single)
	}
	client_map_mutext.Unlock()
	return ret
}

func deregister_device(ip Ipc_packet) {
	logger(PRINT_SUPER_DEBUG, "Taking lock client_map_mutext in deregister_device")
	client_map_mutext.Lock()

	ClientId := ip.ClientId
	logger(PRINT_DEBUG, "Goodbye wrld from ", ClientId)
	// First test to see if the device was already previously registered
	if _, ok := client_map_i[ip.ClientId]; !ok {
		logger(PRINT_WARN, "ClientID is not active, most likely device reconnected")
		client_map_mutext.Unlock()
		return
	}

	// check to see if there is an clientId->device Id relation
	if DeviceId, ok := client_map_i[ClientId]; ok {
		logger(PRINT_DEBUG, " DeviceId: ", DeviceId, "  registered, will be unregistered now")
		delete(client_map, DeviceId)
		delete(client_map_i, ClientId)
	}

	if _, ok := cmd_mux[ClientId]; ok {
		logger(PRINT_WARN, "Deleting state entries from cmd_mux for clientId(now detache):", ClientId)
		logger(PRINT_SUPER_DEBUG, "Taking lock cmd_mux_mutex in deregister_device")
		cmd_mux_mutex.Lock()
		close(cmd_mux[ClientId])
		delete(cmd_mux, ClientId)
		cmd_mux_mutex.Unlock()
	}
	client_map_mutext.Unlock()
}

func register_new_device(ip Ipc_packet) client {
	hp := hello_packet_unpack(ip.P)

	logger(PRINT_NORMAL, "Device Attached", hp)

	logger(PRINT_SUPER_DEBUG, "Taking lock client_map_mutext in register_new_device")
	client_map_mutext.Lock()

	new_client := client{}
	new_client.fw_version = hp.fw_version
	new_client.ClientId = ip.ClientId
	new_client.deviceId = hp.DeviceId /* used as a return */
	new_client.device_name = hp.device_name
	new_client.bricked = hp.bricked
	new_client.attach_time = time.Now()

	DeviceId := hp.DeviceId
	// First test to see if the device was already previously registered
	if client, ok := client_map[DeviceId]; ok {
		logger(PRINT_DEBUG, " DeviceId: ", DeviceId, "  registered before, deleting stale entry")
		// check to see if there are any outstanding cmd_mux entries, if there are, also delete them
		// this probably means we crashed...
		if _, ok := cmd_mux[client.ClientId]; ok {
			logger(PRINT_WARN, "Deleting state entries from cmd_mux for clientId(now detache):", client)
			logger(PRINT_SUPER_DEBUG, "Taking lock cmd_mux_mutex in register_new_device")
			cmd_mux_mutex.Lock()
			close(cmd_mux[client.ClientId])
			delete(cmd_mux, client.ClientId)
			cmd_mux_mutex.Unlock()
		}

		delete(client_map, DeviceId)
		delete(client_map_i, client.ClientId)
	}

	logger(PRINT_NORMAL, " DeviceId: ", DeviceId, " Will be registered to clientId: ", ip.ClientId, "with current FW:", new_client.fw_version, "Bricked code == ", new_client.bricked)
	client_map[DeviceId] = new_client
	client_map_i[new_client.ClientId] = DeviceId
	client_map_mutext.Unlock()

	return new_client
}

func get_device_id_from_client(clientId uint64) (uint64, bool) {
	logger(PRINT_SUPER_DEBUG, "Taking lock cmd_mux_mutex in get_device_id_from_client")
	client_map_mutext.Lock()
	ret, ok := client_map_i[clientId]
	client_map_mutext.Unlock()
	return ret, ok
}

func get_device_fw_version(DeviceId uint64) (bool, uint16) {
	logger(PRINT_SUPER_DEBUG, "Taking lock client_map_mutext in get_device_fw_version")
	client_map_mutext.Lock()
	ret := true
	fw_version := uint16(0)
	if _, ok := client_map[DeviceId]; !ok {
		client_map_mutext.Unlock()
		return false, 0
	}
	fw_version = client_map[DeviceId].fw_version
	client_map_mutext.Unlock()
	return ret, fw_version
}

func handle_incomming_packet(ip Ipc_packet) {
	//first get the type
	t := ip.P.Packet_type
	switch t {
	case HELLO_WORLD_PACKET:
		client := register_new_device(ip)
		if TEST_MODE {
			go test_dispatcher(client)
		} else {

			_, latest_fw_int := latest_fw()
			if latest_fw_int == 0 {
				break
			}

			// FW is outdated, FOTA time
			if client.fw_version < uint16(latest_fw_int) {
				fota(client, uint16(latest_fw_int))
			}
			break
		}

	case CMD_RESPONSE_PACKET:
		cmd_response_mux(ip)
		break
	case FOTA_ACK_PACKET:
		cmd_response_mux(ip)
		break

	case LOGIN_PACKET:
		logger(PRINT_NORMAL, "Login Packet RXed")
		lp := packet_login_unpack(ip.P.Data)
		deviceId, _ := get_device_id_from_client(ip.ClientId)

		/* check to see if there is a cool down period on id, ie, we just added it */

		logger(PRINT_SUPER_DEBUG, "Taking lock cool_down_timer_mutex in LOGIN_PACKET")
		cool_down_timer_mutex.Lock()

		if t, ok := cool_down_map[lp.uid]; ok {
			if time.Now().Sub(t) < time.Second*120 {
				logger(PRINT_NORMAL, "RXed a login within the cooldown period, silently discarding!")

				cool_down_timer_mutex.Unlock()
				return
			}
		}
		cool_down_timer_mutex.Unlock()

		ok := db_put(deviceId, lp.signInOrSignOut, lp.uid, time.Time{})
		if !ok {
			logger_id(PRINT_WARN, deviceId, "unknown user logged in, we will force sync the device!")
			//send sync go device
			c := client{}
			c.ClientId = ip.ClientId
			c.deviceId = deviceId
			db_sync(c, SYNC_NORMAL_MODE)
		}

		break
	case VOID_PACKET:
		logger(PRINT_NORMAL, "RXed a void packet, silently disgarding...")
		break
	case GOODBYE_WORLD_PACKET:
		logger(PRINT_NORMAL, "RXed a goodbye world...", ip.ClientId)
		deregister_device(ip)
		break
	default:
		logger(PRINT_FATAL, "Unknown packet Rxed!!")
	}
}

func handle_incomming_site(ip Ipc_packet) {
	t := ip.P.Packet_type
	switch t {
	case CMD_PACKET:
		be_handle_command(ip)
		break
	case FOTA_PACKET:
		be_handle_fota(ip)
		break
	}
}

func mq_site_to_packet_writter() {
	for {
		rx := make([]byte, PACKET_LEN_MAX)
		n, err := dmq_from_site_to_core.Receive(rx)
		if err != nil {
			log.Fatal("failed to read q")
		}

		p := ipc_packet_unpack(rx[:n])
		go handle_incomming_site(p)
	}
}

func mq_from_packet_to_core() {
	logger(PRINT_NORMAL, "starting packet core listner")
	rx := make([]byte, PACKET_LEN_MAX)
	var n int
	var err error
	for {
		n, err = dmq_from_packet_to_core.Receive(rx)
		if err != nil {
			log.Fatal("failed to read q")
		}
		ip := ipc_packet_unpack(rx[:n])

		go handle_incomming_packet(ip)
	}
}

func init_maps() {
	site_command_mux = make(map[uint64]chan Ipc_packet)
	client_map = make(map[uint64]client)
	client_map_i = make(map[uint64]uint64)
	cmd_mux = make(map[uint64]chan Ipc_packet)
	device_busy = make(map[uint64]bool)
	cool_down_map = make(map[uint32]time.Time)
}

func latest_fw() (string, int) {

	var file_name string
	var latest_fw string
	var latest_fw_int int

	for i := 0; i < 100; i++ {
		file_name = "./fw_versions/timeScan_" + strconv.Itoa(i) + ".bin"
		if _, err := os.Stat(file_name); os.IsNotExist(err) {
			continue
		} else {
			latest_fw = file_name
			latest_fw_int = i
		}
	}
	logger(PRINT_NORMAL, "Latest FW = ", latest_fw)
	return latest_fw, latest_fw_int
}

func sync_devices() {
	deviceArr := get_all_devices()
	for _, v := range deviceArr {
		c := client{}
		c.deviceId = v.DeviceId

		// don't sync bricked devices
		if c.bricked == NOT_BRICKED {
			db_sync(c, SYNC_NORMAL_MODE)
		}
	}
}

// will sync once a month
func sync_devices_timer() {
	for {
		time.Sleep(time.Hour * 23)

		day := time.Now().Day()
		if day == 1 {
			logger(PRINT_NORMAL, "Performing Monthly sync")
			sync_devices()
		}
	}
}

func main() {
	rand.Seed(time.Now().UnixNano())
	logger_init("_server") //must be first

	init_lmq_core()
	init_maps()
	db_connect()

	go sync_devices_timer()

	go xl_archive()
	go mq_from_packet_to_core()
	go mq_site_to_packet_writter()
	go mq_site_listner()

	// will start site, will never return
	main_site()
}
