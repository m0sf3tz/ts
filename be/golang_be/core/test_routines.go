package main

import (
	"fmt"
	"math/rand"
	"runtime"
	"sort"
	"sync"
	"time"
)

var ti_mutex sync.Mutex
var ti uint16

const SERVER_TI_OFFSET_START = 2000

func get_new_transaction_id() uint16 {
	ti_mutex.Lock()
	ti++
	ti = ti % 1000
	ret := ti + SERVER_TI_OFFSET_START
	ti_mutex.Unlock()
	return ret
}

func test_dispatcher(c client) {

	sync_torture_test(c) //tested (only works with a single client!)

	file_test_replace_user(c) //tested, works with 2

	file_test_replace_user_torture(c)

	file_delete_simple(c)

	file_torture_test(c) //tested (with both)

	test_multi_part_command(c)

	test_core_site_ipc_timeout(c)

	fota_test(c)

	test_echo(c)

	test_simple_name(c)

	test_max_name(c)

	ack_stress_test(c)

	test_get_empty_user_list(c)

	test_max_name_plus_1(c)

	test_multi_part_missing_command(c)

	//disconnect_all(c)

	logger(PRINT_DEBUG, "# of goroutines: ", runtime.NumGoroutine())
	PrintMemUsage()
	time.Sleep(time.Second * 30)
}

var letters = []rune("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")

func randSeq(n int) string {
	b := make([]rune, n)
	for i := range b {
		b[i] = letters[rand.Intn(len(letters))]
	}
	return string(b)
}

func test_core_site_ipc_timeout(c client) {
	logger(PRINT_NORMAL, "starting core_site_ipc_timeout test")

	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)
	ipc := create_ipc_cmd_packet(c.deviceId, VOID_CMD)

	be_handle_command(ipc)

	select {
	case <-time.After(time.Second*CMD_TIME_OUT + 5):
		logger(PRINT_FATAL, "Should not have read from response chanllel")
	case resp := <-response_chan:
		cmd_rsp := packet_cmd_response_unpack(resp.P.Data)
		if cmd_rsp.Cmd_status != CMD_TIME_OUT {
			logger(PRINT_FATAL, "Expected command to time out, but it did not...?")
		}
	}

	site_mux_unreg_cmd(c.deviceId)
	logger(PRINT_NORMAL, "Done test_core_site_ipc_timeout test")
}

func login_basic_test(c client) {
	logger_id(PRINT_NORMAL, c.deviceId, "starting basic login test")

	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.ClientId, response_chan)
	ipc := create_ipc_cmd_packet(c.deviceId, SEND_TEST_LOGIN_PACKET)

	be_handle_command(ipc)

	time.Sleep(time.Second * CMD_TIME_OUT)
	site_mux_unreg_cmd(c.ClientId)

	logger_id(PRINT_NORMAL, c.deviceId, "done basic login test")
}

/*
func test_delete_non_existant_user(ip Ipc_packet) {
	logger(PRINT_NORMAL, "starting delete non-test_delete_non_existant_user test")

	// First delete all users
	delete_all_users(ip)

	//delete the first user
	rsp := delete_specific_user(ip, 0)

	if rsp != CMD_STATUS_FAILED_USER_NOT_EXIST {
		logger(PRINT_FATAL, "Tried to delete a user that does not exist, should have gotten rsp CMD_STATUS_FAILED_USER_NOT_EXIST, but got: ", rsp)
	}

	logger(PRINT_NORMAL, "done delete non-test_delete_non_existant_user test")
}
*/

/*
func test_delete_specific_user(ip Ipc_packet) {
	// First delete all users
	delete_all_users(ip)

	//Now add a random person
	l := rand.Intn(MAX_USER_NAME_LEN) + 1
	r := randSeq(l)
	logger(PRINT_NORMAL, "Adding user: ", r, "with len ", l)
	test_add_user(ip, r, CMD_STATUS_GOOD)

	//get all the names (should just be one)
	pass, v, _ := get_all_users(ip, false)
	fmt.Println(pass)
	if !pass {
		logger(PRINT_FATAL, "failed to get all names")
	}

	if len(v) != 1 {
		logger(PRINT_FATAL, "got more responses than expected, len ==", len(v))
	}

	if v[0].name != r {
		logger(PRINT_FATAL, "names did not match on test_simple_name, V[0] ==", v[0].name, "len == ", len(v[0].name))
	}

	//delete the first user
	delete_specific_user(ip, 0)

	pass, _, reason := get_all_users(ip,false)
	if !pass {
		logger(PRINT_FATAL, "requested an empty user list, should not have failed")
	}
	if reason != CMD_STATUS_FAILED_MEM_EMTPY {
		logger(PRINT_FATAL, "return command status on getting empty list was cnot CMD_STATUS_FAILED_MEM_EMTPY")
	}
}
*/

func test_get_empty_user_list(c client) {
	logger(PRINT_NORMAL, "Starting test get empty user list test")
	// First delete all users
	delete_all_users(c)

	pass, _, reason := get_all_users(c, false)
	if !pass {
		logger(PRINT_FATAL, "requested an empty user list, should have passed")
	}
	if reason != CMD_STATUS_FAILED_MEM_EMTPY {
		logger(PRINT_FATAL, "return command status on getting empty list was cnot CMD_STATUS_FAILED_MEM_EMTPY")
	}
	logger(PRINT_NORMAL, "Finished get empty user list test, got response == ", reason)
}

func file_delete_simple(c client) {
	logger_id(PRINT_NORMAL, c.deviceId, "Starting fiel_delet_simple")

	testName := "bob"
	slotId := uint16(0)

	// First delete all users
	delete_all_users(c)

	//Now add a random person
	test_add_user(c, testName, CMD_STATUS_GOOD, DONT_CARE_UID, DONT_REPLACE_USER)

	//get all the names
	pass, v, _ := get_all_users(c, false)
	fmt.Println(pass)
	if !pass {
		logger_id(PRINT_FATAL, c.ClientId, "failed to get all names")
	}
	if v[0].name != testName {
		logger_id(PRINT_FATAL, c.deviceId, "names did not match on fiel_delete_simple:", v[0].name)
	}

	if len(v) != 1 {
		logger_id(PRINT_FATAL, c.deviceId, "got more responses than expected, len ==", len(v))
	}

	//first time should be fine
	test_delete_user(c, CMD_STATUS_GOOD, slotId)

	//second time shuold fail
	test_delete_user(c, CMD_STATUS_FAILED_USER_NOT_EXIST, slotId)

	logger_id(PRINT_NORMAL, c.deviceId, "done fiel_delet_simple")
}

func test_simple_name(c client) {
	logger_id(PRINT_NORMAL, c.deviceId, "Starting simeple name test")
	// First delete all users
	delete_all_users(c)

	//Now add a random person
	l := rand.Intn(MAX_USER_NAME_LEN) + 1
	r := randSeq(l)
	logger_id(PRINT_NORMAL, c.deviceId, "Adding user: ", r, "with len ", l)
	test_add_user(c, r, CMD_STATUS_GOOD, DONT_CARE_UID, DONT_REPLACE_USER)

	//get all the names
	pass, v, _ := get_all_users(c, false)
	fmt.Println(pass)
	if !pass {
		logger_id(PRINT_FATAL, c.ClientId, "failed to get all names")
	}
	if v[0].name != r {
		logger_id(PRINT_FATAL, c.deviceId, "names did not match on test_simple_name, V[0] ==", v[0].name, "len == ", len(v[0].name))
	}

	if len(v) != 1 {
		logger_id(PRINT_FATAL, c.deviceId, "got more responses than expected, len ==", len(v))
	}

	logger_id(PRINT_NORMAL, c.deviceId, "Done simple name test")
}

func test_max_name_plus_1(c client) {
	// First delete all users
	delete_all_users(c)

	nameArr := make([]string, 0, MAX_USERS_DEVICE)
	deviceArrSorted := make([]string, MAX_USERS_DEVICE)
	test := MAX_USERS_DEVICE
	var uid uint32

	//Now add a random person
	for i := 0; i < test; i++ {
		l := MAX_USER_NAME_LEN
		r := randSeq(l)

		nameArr = append(nameArr, r)
		logger(PRINT_NORMAL, "Adding user: ", r, "with len ", l)
		test_add_user(c, r, CMD_STATUS_GOOD, uid, DONT_REPLACE_USER)
		uid++
	}
	// Sort the user arr
	sort.Strings(nameArr)

	// we expect this to FAIL, but we should get the rest of the names back normally
	test_add_user(c, "should not matter", CMD_STATUS_FAILED_MEM_FULL, uid, DONT_REPLACE_USER)
	uid++

	for i, v := range nameArr {
		fmt.Println(i, ":", v)
	}

	pass, deviceArr, _ := get_all_users(c, false)
	if !pass {
		logger(PRINT_FATAL, "failed to get all names")
	}

	fmt.Println("device reponded with..")
	for i, v := range deviceArr {
		fmt.Println(i, ":", v)
	}

	// first sort the reponse arr
	for i := 0; i < test; i++ {
		deviceArrSorted[i] = deviceArr[i].name
	}
	sort.Strings(deviceArrSorted)

	for i := 0; i < test; i++ {
		if deviceArrSorted[i] != nameArr[i] {
			logger(PRINT_FATAL, "Miss-match i=", i, " names, wrote: ", nameArr[i], "got: ", deviceArrSorted[i])
		}
	}

	logger(PRINT_NORMAL, "test_max_name_plus_1 test passed")
}

func test_max_name(c client) {
	nameArr := make([]string, 0, MAX_USERS_DEVICE)
	deviceArrSorted := make([]string, MAX_USERS_DEVICE)
	test := MAX_USERS_DEVICE

	// First delete all users
	delete_all_users(c)

	//Now add a random person
	for i := 0; i < test; i++ {
		l := MAX_USER_NAME_LEN
		r := randSeq(l)

		nameArr = append(nameArr, r)
		logger_id(PRINT_NORMAL, c.deviceId, "Adding user: ", r, "with len ", l)
		test_add_user(c, r, CMD_STATUS_GOOD, uint32(i), DONT_REPLACE_USER)
	}

	// Sort the user arr
	sort.Strings(nameArr)

	for i, v := range nameArr {
		fmt.Println(i, ":", v)
	}

	pass, deviceArr, _ := get_all_users(c, false)
	if !pass {
		logger_id(PRINT_FATAL, c.deviceId, "failed to get all names")
	}

	for i, v := range deviceArr {
		fmt.Println(i, ":", v)
	}

	// first sort the reponse arr
	for i := 0; i < test; i++ {
		deviceArrSorted[i] = deviceArr[i].name
	}
	sort.Strings(deviceArrSorted)

	for i := 0; i < test; i++ {
		if deviceArrSorted[i] != nameArr[i] {
			logger_id(PRINT_FATAL, c.deviceId, "Miss-match i=", i, " names, wrote: ", nameArr[i], "got: ", deviceArrSorted[i])
		}
	}

	logger_id(PRINT_NORMAL, c.deviceId, "test_max_name test passed")
}

func test_echo(c client) {
	count := 3000

	for {
		response_chan := make(chan Ipc_packet)
		site_mux_reg_cmd(c.deviceId, response_chan)

		ipc := create_ipc_cmd_packet(c.deviceId, ECHO_CMD)
		ipc.ClientId = c.ClientId
		ipc.DeviceId = c.deviceId

		go be_handle_command(ipc)

		select {
		case resp := <-response_chan:
			cmd_rsp := packet_cmd_response_unpack(resp.P.Data)
			if cmd_rsp.Cmd_status != CMD_STATUS_GOOD {
				logger_id(PRINT_FATAL, c.deviceId, "Unexpected NAK")
			}
			break
		case <-time.After(time.Second * 3):
			logger_id(PRINT_FATAL, c.deviceId, "Timed out during lost ack test on the first ACH")
		}

		site_mux_unreg_cmd(c.deviceId)
		count--
		if count == 0 {
			break
		}
	}
}

func test_lost_ack(c client) {
	/* First ask the device to time out the next command ACK*/
	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)
	ipc := create_ipc_cmd_packet(c.deviceId, TIME_OUT_NEXT_PACKET)

	be_handle_command(ipc)

	select {
	case resp := <-response_chan:
		cmd_rsp := packet_cmd_response_unpack(resp.P.Data)
		if cmd_rsp.Cmd_status != CMD_STATUS_GOOD {
			logger(PRINT_FATAL, "Unexpected NAK")
		}
		break
	case <-time.After(time.Second):
		logger(PRINT_FATAL, "Timed out during lost ack test on the first ACH")
	}

	/* Now send an ECHO command, this should fail with a DEVICE ACK timeout*/
	ipc = create_ipc_cmd_packet(c.deviceId, VOID_CMD)
	be_handle_command(ipc)

	select {
	case resp := <-response_chan:
		cmd_rsp := packet_cmd_response_unpack(resp.P.Data)

		if cmd_rsp.Cmd_status == CMD_ACK_TIMED_OUT {
			logger(PRINT_NORMAL, "Missing ACK test passed!")
			break
		}
		logger(PRINT_FATAL, "Unexpected NAK")
		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME * 2): /* *2 as a fudge factor */
		logger(PRINT_FATAL, "Timed out during lost ack test on the SECOND ACH")
	}
}

func test_multi_part_command(c client) {
	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)
	ipc := create_ipc_cmd_packet(c.deviceId, SEND_MULTI_PART_RSP)

	/* +1 is so we don't request a zero sized packet */
	packets := rand.Intn(150) + 1

	/* the second byte of the payload is how many packets to request back */
	ipc.P.Data[MULTI_PART_TEST_PACKETS_COUNT_BYTE_OFFSET] = byte(packets)
	logger(PRINT_NORMAL, "starting multi part response test, asking for ", packets, "packets")

	counter := 0
	resp_arr := make(map[int]Ipc_packet)

	go be_handle_command(ipc)

	for {
		select {
		case resp := <-response_chan:

			resp_arr[int(get_packet_sequence_number(resp))] = resp
			counter++

			fmt.Println(packets)
			if counter == packets {
				goto check
			}
		case <-time.After(100 * time.Second):
			logger(PRINT_FATAL, "Timed out during multi_part_packet_test")
		}
	}
check:
	if len(resp_arr) != packets {
		logger(PRINT_FATAL, "Len = ", len(resp_arr), "should be", 100)
	}
	for i := 0; i < packets; i++ {
		rsp := packet_cmd_response_unpack((resp_arr[i]).P.Data)
		if rsp.Packets_sequence_number != uint8(i) || rsp.Resp_payload[MULTI_PART_TEST_DEVICE_PACKET_COUNTER] != uint8(i) {
			fmt.Println("I = ", i, "packet =", resp_arr[i])
			panic(0)
		}
	}
	fmt.Println("Multipart test PASSED!")
}

func test_multi_part_missing_command(c client) {
	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)
	ipc := create_ipc_cmd_packet(c.deviceId, SEND_MULTI_PART_RSP_FAIL)

	logger(PRINT_NORMAL, "starting multi part missing packet test")

	go be_handle_command(ipc)

	for {
		select {
		case resp := <-response_chan:
			cmd_rsp := packet_cmd_response_unpack(resp.P.Data)
			if cmd_rsp.Cmd_status == CMD_TIME_OUT {
				goto done
			}
		case <-time.After(time.Second * COMMAND_TIMEOUT_TIME * 2): /* Two times for a fudge factor */
			logger(PRINT_FATAL, "multi test missing packet FAILED!")
		}
	}
done:
	fmt.Println("Multipart mising response test PASSED!")
}

func create_ipc_cmd_packet(DeviceId uint64, Type uint8) Ipc_packet {
	cmd := Cmd_payload{}
	cmd.Cmd_payload = []byte("N/A")
	cmd.Cmd_type = Type

	ipc := Ipc_packet{}
	ipc.DeviceId = DeviceId
	ipc.P.Packet_type = CMD_PACKET

	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED

	ipc.P.Data = packet_pack_cmd(cmd)

	return ipc
}

func print_user_model(model []employee) {
	for _, v := range model {
		fmt.Println("id: ", v.id, " name:", v.name, " valid:", v.valid)
	}
}

func print_model_and_device(model []employee, deviceArr []employee) {
	fmt.Println("model")
	fmt.Println(model)
	fmt.Println("DeviceArr")
	fmt.Println(deviceArr)
}

func compare_versus_model(model []employee, deviceArr []employee, c client) {
	device_arr_expanded := make([]employee, MAX_USERS_DEVICE)

	//need to "expand" teh device arr into something we can easily compare against the model
	for _, v := range deviceArr {
		device_arr_expanded[v.id].valid = v.valid
		device_arr_expanded[v.id].name = v.name
		device_arr_expanded[v.id].uid = v.uid
	}

	for i := 0; i < MAX_USERS_DEVICE; i++ {
		if model[i].valid != device_arr_expanded[i].valid {
			fmt.Println("model")
			fmt.Println(model)
			fmt.Println("DeviceArr")
			fmt.Println(deviceArr)
			logger_id(PRINT_FATAL, c.deviceId, "miss-match, id:", i, "model[i].valid", model[i].valid, "deviceArrExpanded[i].valid:", device_arr_expanded[i].valid)
		}
		if model[i].valid {
			if model[i].name != device_arr_expanded[i].name {
				fmt.Println("model")
				fmt.Println(model)
				fmt.Println("DeviceArr")
				fmt.Println(deviceArr)
				logger_id(PRINT_FATAL, c.deviceId, "miss-match, id:", i, "model[i].name", model[i].name, "device_arr_expanded[i].name:", device_arr_expanded[i].name)
			} else {
				logger_id(PRINT_NORMAL, c.deviceId, "")
				logger_id(PRINT_NORMAL, c.deviceId, "Model = ", model[i])
				logger_id(PRINT_NORMAL, c.deviceId, "deviceArr = ", device_arr_expanded[i])
				logger_id(PRINT_NORMAL, c.deviceId, "Models MATCH!")
				logger_id(PRINT_NORMAL, c.deviceId, "")
			}
		}
	}
}

func add_user_model(model []employee, name string, replace bool, uid uint32) bool {
	var user_exists bool
	var user_slot uint16

	for i, _ := range model {
		if replace {
			if model[i].uid == uid {
				user_exists = true
				user_slot = uint16(i)
				break
			}
		}
	}

	if user_exists {
		model[user_slot].valid = true
		model[user_slot].id = uint16(user_slot) //slot id
		model[user_slot].name = name
		model[user_slot].uid = uid
		return true
	} else {
		for i, v := range model {
			if v.valid == false {
				model[i].valid = true
				model[i].id = uint16(i) //slot id
				model[i].name = name
				model[i].uid = uid
				return true
			}
		}
	}
	return false
}

func delete_user_model(model []employee, id uint16) bool {
	if model[id].valid == true {
		model[id].name = ""
		model[id].valid = false
		model[id].uid = 0
		model[id].id = 0
		return true
	}
	return false
}

func model_get_valid_userid_random(model []employee) (uint16, uint32) {
	// check to see if there are any valid entries at all
	not_empty := false
	for _, v := range model {
		if v.valid == true {
			not_empty = true
		}
	}
	if not_empty {
		for {
			i := rand.Intn(MAX_USERS_DEVICE)
			if model[i].valid == true {
				return uint16(i), model[i].uid
			}
		}
	} else {
		return uint16(INVALID_USER_SLOT), 0
	}
}

func file_torture_test_simple(c client) {
	// First delete all users
	delete_all_users(c)

	//make a model
	model := make([]employee, MAX_USERS_DEVICE)

	//add a user to both the model and device
	test_add_user(c, "Saman", CMD_STATUS_GOOD, DONT_CARE_UID, DONT_REPLACE_USER)
	add_user_model(model, "Saman", false, DONT_CARE_UID)

	pass, deviceArr, _ := get_all_users(c, false)
	if !pass {
		logger_id(PRINT_FATAL, c.deviceId, "got an error getting list of users!")
	}

	compare_versus_model(model, deviceArr, c)

	//delete the first user
	pass = delete_user_model(model, 0)
	delete_specific_user(c, 0, false)

	if !pass {
		logger_id(PRINT_FATAL, c.deviceId, "Could not delete from model!")
	}

	pass, deviceArr, reason := get_all_users(c, false)
	if reason != CMD_STATUS_FAILED_MEM_EMTPY {
		logger_id(PRINT_FATAL, c.deviceId, "return command status on getting empty list was cnot CMD_STATUS_FAILED_MEM_EMTPY, it was:", reason)
	}

	compare_versus_model(model, deviceArr, c)

	// try to delete an already non-existant user
	pass = delete_user_model(model, 0)
	resp := delete_specific_user(c, 0, false)

	if resp != CMD_STATUS_FAILED_USER_NOT_EXIST || pass != false {
		logger_id(PRINT_FATAL, c.deviceId, "model/device did not react right to deleting none-existant user")
	}
	compare_versus_model(model, deviceArr, c)

	//Now fill up the device
	for i := 0; i < MAX_USERS_DEVICE; i++ {
		l := MAX_USER_NAME_LEN
		r := randSeq(l)

		logger_id(PRINT_NORMAL, c.deviceId, "Adding user: ", r, "with len ", l)
		test_add_user(c, r, CMD_STATUS_GOOD, DONT_CARE_UID, DONT_REPLACE_USER)
		add_user_model(model, r, false, DONT_CARE_UID)
	}

	pass, deviceArr, _ = get_all_users(c, false)
	if !pass {
		logger_id(PRINT_FATAL, c.deviceId, "return command status on getting empty list was cnot CMD_STATUS_FAILED_MEM_EMTPY")
	}

	compare_versus_model(model, deviceArr, c)
}

func sync_torture_test(c client) {
	sync_normal(c)
	sync_max_rm_some(c)
	sync_max_rm_all(c)
}

func sync_min(c client) {
	name := randSeq(10)
	test_add_employee(name)

	emp_arr := db_get_employees()
	if len(emp_arr) != 1 {
		panic(0)
	}

	for _, emp := range emp_arr {
		test_add_user(c, emp.Name, CMD_STATUS_GOOD, emp.Id, DONT_REPLACE_USER)
	}

	db_sync(c, SYNC_TEST_MODE)
}

func sync_max_rm_all(c client) {
	// First delete all users
	delete_all_users(c)

	// Delete DB
	db_truncate_employeeifo()

	users_to_add := MAX_USERS_DEVICE
	for i := 0; i < users_to_add; i++ {
		fmt.Println(i)
		name := randSeq(MAX_USER_NAME_LEN)
		test_add_employee(name)
	}

	emp_arr := db_get_employees()
	if len(emp_arr) != users_to_add {
		panic(0)
	}

	for _, emp := range emp_arr {
		test_add_user(c, emp.Name, CMD_STATUS_GOOD, emp.Id, DONT_REPLACE_USER)
	}

	db_truncate_employeeifo()

	db_sync(c, SYNC_TEST_MODE)

	_, emp_arr_device, _ := get_all_users(c, false)

	db_sync_compare(emp_arr_device)
}

func sync_max_rm_some(c client) {
	// First delete all users
	delete_all_users(c)

	// Delete DB
	db_truncate_employeeifo()

	users_to_add := MAX_USERS_DEVICE
	for i := 0; i < users_to_add; i++ {
		fmt.Println(i)
		name := randSeq(MAX_USER_NAME_LEN)
		test_add_employee(name)
	}

	emp_arr := db_get_employees()
	if len(emp_arr) != users_to_add {
		panic(0)
	}

	for _, emp := range emp_arr {
		test_add_user(c, emp.Name, CMD_STATUS_GOOD, emp.Id, DONT_REPLACE_USER)
	}

	for _, v := range emp_arr {
		if rand.Intn(10) > 5 {
			db_delete_user(v.Id)
			fmt.Println("Deleting user: ", v.Id)
		}
	}

	db_sync(c, SYNC_TEST_MODE)

	_, emp_arr_device, _ := get_all_users(c, false)

	db_sync_compare(emp_arr_device)
}

func sync_normal(c client) {
	// First delete all users
	delete_all_users(c)

	// Delete DB
	db_truncate_employeeifo()

	users_to_add := rand.Intn(50)
	for i := 0; i < users_to_add; i++ {
		fmt.Println(i)
		name := randSeq(MAX_USER_NAME_LEN)
		test_add_employee(name)
	}

	emp_arr := db_get_employees()
	if len(emp_arr) != users_to_add {
		panic(0)
	}

	for _, emp := range emp_arr {
		test_add_user(c, emp.Name, CMD_STATUS_GOOD, emp.Id, DONT_REPLACE_USER)
	}

	for _, v := range emp_arr {
		if rand.Intn(10) > 5 {
			db_delete_user(v.Id)
			fmt.Println("Deleting user: ", v.Id)
		}
	}

	db_sync(c, SYNC_TEST_MODE)

	_, emp_arr_device, _ := get_all_users(c, false)

	db_sync_compare(emp_arr_device)
}

func file_test_replace_user(c client) {
	logger_id(PRINT_NORMAL, c.deviceId, "Starting replace test user!")
	// First delete all users
	delete_all_users(c)

	//uid
	var uid uint32
	/*
		uid = rand.Uint32()

		// first add the user
		r := randSeq(MAX_USER_NAME_LEN)
		test_add_user(c, r, CMD_STATUS_GOOD, uid, DONT_REPLACE_USER)

		// trying adding the user again, we should get denied
		r = randSeq(MAX_USER_NAME_LEN)
		test_add_user(c, r, CMD_STATUS_UID_EXISTS, uid, DONT_REPLACE_USER)

		// trying adding the user again, this time it should go through
		r = randSeq(MAX_USER_NAME_LEN)
		test_add_user(c, r, CMD_STATUS_GOOD, uid, REPLACE_USER)

		pass, deviceArr, _ := get_all_users(c, false)

		if !pass {
			logger_id(PRINT_FATAL, c.deviceId, "Failed to get users!")
		}

		if len(deviceArr) != 1 {
			logger_id(PRINT_FATAL, c.deviceId, "should have only gotten one user back!")
		}

		if deviceArr[0].name != r {
			logger_id(PRINT_FATAL, c.deviceId, "Name not what we expect!")
		}

		if deviceArr[0].uid != uid {
			logger_id(PRINT_FATAL, c.deviceId, "UID not what we expect!")
		}
	*/
	//Add 2 users.
	//delete the first one
	//replace user 2
	//make sure proper ID is picked

	// First delete all users
	delete_all_users(c)

	r := "don't care"
	uid = 0

	test_add_user(c, r, CMD_STATUS_GOOD, uid, REPLACE_USER)
	uid++

	test_add_user(c, r, CMD_STATUS_GOOD, uid, REPLACE_USER)
	uid++

	//delete the first user
	delete_specific_user(c, 0, false)

	// add UID = 1
	test_add_user(c, r, CMD_STATUS_GOOD, 1, REPLACE_USER)

	// get All users
	pass, deviceArr, _ := get_all_users(c, false)
	if !pass {
		logger_id(PRINT_FATAL, c.deviceId, "return command status on getting user list in tourure simple test was not good!")
	}
	if len(deviceArr) != 1 {
		logger_id(PRINT_FATAL, c.deviceId, "multiple users exist in replace simlpe test device arr!")
	}

	if deviceArr[0].id != 1 {
		logger_id(PRINT_FATAL, c.deviceId, "incorrect id slot!, should be", 1, "but it is", deviceArr[0].id)
	}

	if deviceArr[0].uid != 1 {
		logger_id(PRINT_FATAL, c.deviceId, "incorrect uid!")
	}

	logger_id(PRINT_NORMAL, c.deviceId, "Done replace test user!")
}

func file_test_replace_user_torture(c client) {
	logger_id(PRINT_NORMAL, c.deviceId, "Starting replace torture test!")

	// First delete all users
	delete_all_users(c)

	//make a model
	model := make([]employee, MAX_USERS_DEVICE)

	var uid uint32

	//Now fill device
	for i := 0; i < MAX_USERS_DEVICE; i++ {
		l := MAX_USER_NAME_LEN
		r := randSeq(l)

		logger_id(PRINT_NORMAL, c.deviceId, "Adding user: ", r, "with len ", l)
		test_add_user(c, r, CMD_STATUS_GOOD, uint32(i), DONT_REPLACE_USER)

		stat := add_user_model(model, r, false, uid)
		if !stat {
			logger(PRINT_FATAL, "Got an unexpted error! - could not fill device")
		}
		uid++
	}

	//this willt ry to add one more, will fail
	name := "blah"
	test_add_user(c, name, CMD_STATUS_FAILED_MEM_FULL, uid, DONT_REPLACE_USER)
	stat := add_user_model(model, name, false, uid)
	if stat {
		logger(PRINT_FATAL, "Got an unexpted error! - got an extra user in!")
	}
	pass, deviceArr, _ := get_all_users(c, false)
	if !pass {
		logger_id(PRINT_FATAL, c.deviceId, "return command status on getting user list after trying to add +1 user was not good")
	}
	logger_id(PRINT_NORMAL, c.deviceId, "COMPARING MODELS!")
	compare_versus_model(model, deviceArr, c)

	//replace a bunch of users, this time user replace, this should be ok
	for i := 0; i < 30; i++ {
		id, uid := model_get_valid_userid_random(model)
		r := model[id].name
		test_add_user(c, r, CMD_STATUS_GOOD, uid, REPLACE_USER)
	}
	pass, deviceArr, _ = get_all_users(c, false)
	if !pass {
		logger_id(PRINT_FATAL, c.deviceId, "return command status on getting list of uers after replaceing a bunch of users was not good")
	}
	logger_id(PRINT_NORMAL, c.deviceId, "COMPARING MODELS!")
	compare_versus_model(model, deviceArr, c)

	// Start Again..
	delete_all_users(c)
	model = make([]employee, MAX_USERS_DEVICE)

	//Now add a bunch of users, this time with replace, should be ok
	uid = 0
	for i := 0; i < 50; i++ {
		// add user
		l := MAX_USER_NAME_LEN
		r := randSeq(l)

		logger_id(PRINT_NORMAL, c.deviceId, "Adding user: ", r, "with len ", l)
		test_add_user(c, r, CMD_STATUS_GOOD, uint32(i), REPLACE_USER)

		stat := add_user_model(model, r, true, uid)
		if !stat {
			logger(PRINT_FATAL, "Got an unexpted error! - could not fill device")
		}
		uid++
	}
}

func file_torture_test(c client) {
	// First delete all users
	delete_all_users(c)

	//uid
	var uid uint32

	//make a model
	model := make([]employee, MAX_USERS_DEVICE)

	total_cycles := 5
	total_add_delete_per_cycle := MAX_USERS_DEVICE + 75

	//first Test adding more than deleting
	threshold := 5
	cycles_left := total_cycles
	add_delete_cycles_left := total_add_delete_per_cycle

	for {
		t := rand.Intn(100)

		if t > threshold {
			r := randSeq(MAX_USER_NAME_LEN)
			logger_id(PRINT_NORMAL, c.deviceId, "Adding user: ", r)

			//If the "model is full, we expect add to fail"
			uid = uid + 1
			stat := add_user_model(model, r, false, uid)
			if stat {
				test_add_user(c, r, CMD_STATUS_GOOD, uid, DONT_REPLACE_USER)
			} else {
				test_add_user(c, r, CMD_STATUS_FAILED_MEM_FULL, uid, DONT_REPLACE_USER)
			}

			if add_delete_cycles_left%10 == 0 {
				pass, deviceArr, _ := get_all_users(c, false)
				if !pass {
					logger_id(PRINT_FATAL, c.deviceId, "return command status on getting empty list was cnot CMD_STATUS_FAILED_MEM_EMTPY")
				}
				logger_id(PRINT_NORMAL, c.deviceId, "COMPARING MODELS!")
				compare_versus_model(model, deviceArr, c)
				print_model_and_device(model, deviceArr)
			}
		} else {
			//replace OR delete in this branch
			coin := rand.Intn(20)
			if coin > 2 {
				id, _ := model_get_valid_userid_random(model)
				if id != INVALID_USER_SLOT {
					logger_id(PRINT_NORMAL, c.deviceId, "Deleting a user with ID:", id)

					if !delete_user_model(model, id) {
						logger_id(PRINT_FATAL, c.deviceId, "was not able to delete from model user with id = ", id)
					}

					delete_specific_user(c, id, false)

					if add_delete_cycles_left%10 == 0 {
						pass, deviceArr, reason := get_all_users(c, false)
						if !pass {
							logger_id(PRINT_FATAL, c.deviceId, "Error getting list of users: resp,reason = ", pass, reason)
						}
						logger_id(PRINT_NORMAL, c.deviceId, "COMPARING MODELS!")
						compare_versus_model(model, deviceArr, c)
					}
				}
			} else {
				// replace branch
				id, _ := model_get_valid_userid_random(model)
				if id != INVALID_USER_SLOT {
					logger_id(PRINT_NORMAL, c.deviceId, "Replacing user with ID:", id, "and UID:", model[id].uid)

					// replace does not need a user name
					r := randSeq(MAX_USER_NAME_LEN)
					logger_id(PRINT_NORMAL, c.deviceId, "Will update user with name ", model[id].name, "with name", r)
					model[id].name = r

					test_add_user(c, r, CMD_STATUS_GOOD, model[id].uid, REPLACE_USER)
				}
			}
		}

		add_delete_cycles_left--

		if 0 == add_delete_cycles_left {
			if cycles_left != 0 {
				if threshold == 5 {
					threshold = 95
				} else {
					threshold = 5
				}
				logger_id(PRINT_NORMAL, c.deviceId, "One cycle finished, starting new one with threshodl := ", threshold)
				add_delete_cycles_left = total_add_delete_per_cycle
				cycles_left--
				continue
			}
			logger_id(PRINT_NORMAL, c.deviceId, "Done torture test!!!")
			pass, deviceArr, reason := get_all_users(c, false)
			if !pass {
				logger_id(PRINT_FATAL, c.deviceId, "Error getting list of users: resp,reason = ", pass, reason)
			}
			print_model_and_device(model, deviceArr)
			break
		}
	}
}

func ack_stress_test(c client) {
	response_chan := make(chan Ipc_packet)
	site_mux_reg_cmd(c.deviceId, response_chan)
	ipc := create_ipc_server_side_ack_stress_test(c.deviceId, c.ClientId)

	go be_handle_command(ipc)

	var resp Ipc_packet
	select {
	case resp = <-response_chan:
		cmd_rsp := packet_cmd_response_unpack(resp.P.Data)

		if cmd_rsp.Cmd_status != CMD_STATUS_GOOD {
			logger_id(PRINT_FATAL, c.deviceId, "Test failed!, expected CMD_RESPONSE_GOOD got", cmd_rsp.Cmd_status)
		}

		break
	case <-time.After(time.Second * COMMAND_TIMEOUT_TIME * 100):
		logger_id(PRINT_FATAL, c.deviceId, "Timed out resetting device!")
	}
	site_mux_unreg_cmd(c.deviceId)
}

func create_ipc_server_side_ack_stress_test(deviceId uint64, clientID uint64) Ipc_packet {
	cmd := Cmd_payload{}
	cmd.Cmd_payload = []byte("N/A")
	cmd.Cmd_type = ACK_STRESS_TEST

	ipc := Ipc_packet{}
	ipc.ClientId = clientID
	ipc.DeviceId = deviceId
	ipc.P.Packet_type = CMD_PACKET
	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED
	ipc.P.Data = packet_pack_cmd(cmd)

	return ipc
}

func create_ipc_display_msg(deviceId uint64, msg uint8) Ipc_packet {
	cmd := Cmd_payload{}
	cmd.Cmd_payload = make([]byte, 1)
	cmd.Cmd_payload[0] = byte(msg)
	cmd.Cmd_type = DISPLAY_MSG_LCD

	ipc := Ipc_packet{}
	ipc.DeviceId = deviceId
	ipc.P.Packet_type = CMD_PACKET
	ipc.P.Transaction_id = get_new_transaction_id()
	ipc.P.Consumer_ack_req = CONSUMER_ACK_REQUIRED
	ipc.P.Data = packet_pack_cmd(cmd)

	return ipc
}

func PrintMemUsage() {
	var m runtime.MemStats
	runtime.ReadMemStats(&m)
	// For info on each, see: https://golang.org/pkg/runtime/#MemStats
	fmt.Printf("Alloc = %v MiB", bToMb(m.Alloc))
	fmt.Printf("\tTotalAlloc = %v MiB", bToMb(m.TotalAlloc))
	fmt.Printf("\tSys = %v MiB", bToMb(m.Sys))
	fmt.Printf("\tNumGC = %v\n", m.NumGC)
}

func bToMb(b uint64) uint64 {
	return b / 1024 / 1024
}

func test_add_employee(name string) {
	test_cmd := Cmd{}
	test_cmd.Name = name
	test_cmd.Email = randSeq(MAX_USER_NAME_LEN) + "@gmail.com"
	test_cmd.Salary = "50000"

	/* must have ONE shift */
	test_cmd.Shift_m_s = time.Date(2000, 1, 1, 7, 0, 0, 0, time.UTC).Add(time.Second * 0 * time.Duration(rand.Int31n(3600*2)-3600)).Format(TIME_FORMAT_SHIFT)
	test_cmd.Shift_m_e = time.Date(2000, 1, 1, 17, 0, 0, 0, time.UTC).Add(time.Second * 0 * time.Duration(rand.Int31n(3600*2)-3600)).Format(TIME_FORMAT_SHIFT)

	if rand.Int31n(2) == 1 {
		test_cmd.Shift_t_s = time.Date(2000, 1, 1, 7, 0, 0, 0, time.UTC).Add(time.Second * 0 * time.Duration(rand.Int31n(3600*2)-3600)).Format(TIME_FORMAT_SHIFT)
		test_cmd.Shift_t_e = time.Date(2000, 1, 1, 17, 0, 0, 0, time.UTC).Add(time.Second * 0 * time.Duration(rand.Int31n(3600*2)-3600)).Format(TIME_FORMAT_SHIFT)
	}

	if rand.Int31n(2) == 1 {
		test_cmd.Shift_w_s = time.Date(2000, 1, 1, 7, 0, 0, 0, time.UTC).Add(time.Second * 0 * time.Duration(rand.Int31n(3600*2)-3600)).Format(TIME_FORMAT_SHIFT)
		test_cmd.Shift_w_e = time.Date(2000, 1, 1, 17, 0, 0, 0, time.UTC).Add(time.Second * 0 * time.Duration(rand.Int31n(3600*2)-3600)).Format(TIME_FORMAT_SHIFT)
	}

	if rand.Int31n(2) == 1 {
		test_cmd.Shift_th_s = time.Date(2000, 1, 1, 7, 0, 0, 0, time.UTC).Add(time.Second * 0 * time.Duration(rand.Int31n(3600*2)-3600)).Format(TIME_FORMAT_SHIFT)
		test_cmd.Shift_th_e = time.Date(2000, 1, 1, 17, 0, 0, 0, time.UTC).Add(time.Second * 0 * time.Duration(rand.Int31n(3600*2)-3600)).Format(TIME_FORMAT_SHIFT)
	}

	if rand.Int31n(2) == 1 {
		test_cmd.Shift_f_s = time.Date(2000, 1, 1, 7, 0, 0, 0, time.UTC).Add(time.Second * 0 * time.Duration(rand.Int31n(3600*2)-3600)).Format(TIME_FORMAT_SHIFT)
		test_cmd.Shift_f_e = time.Date(2000, 1, 1, 17, 0, 0, 0, time.UTC).Add(time.Second * 0 * time.Duration(rand.Int31n(3600*2)-3600)).Format(TIME_FORMAT_SHIFT)
	}
	fmt.Println(db_add_employee_salary_shift(test_cmd, NEW_USER))
}
