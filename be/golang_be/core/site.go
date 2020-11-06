package main

import (
	"encoding/gob"
	"encoding/json"
	"github.com/gorilla/sessions"
	"github.com/gorilla/websocket"
	"html/template"
	"log"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"
)

//session related
var valid_sessions map[string]int64
var store *sessions.CookieStore

func get_random_string() string {
	f, _ := os.Open("/dev/urandom")
	b := make([]byte, 128)
	f.Read(b)
	logger(PRINT_NORMAL, b)
	f.Close()
	return string(b)
}

var upgrader = websocket.Upgrader{CheckOrigin: CO} // use default options

func CO(r *http.Request) bool {
	logger(PRINT_NORMAL, r.Header.Get("Origin"))
	return true
}

func upgrade(w http.ResponseWriter, r *http.Request) {
	logger(PRINT_NORMAL, "Upgradding websocket...")
	c, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Print("upgrade:", err)
		return
	}

	l_chan := make(chan Ipc_packet)

	defer func() {
		logger(PRINT_NORMAL, "Closing websocket...")
		c.Close()
		close(l_chan)
	}()

	for {
		mt, message, err := c.ReadMessage()
		if err != nil {
			logger(PRINT_NORMAL, err)
			break
		}

		if len(message) == 0 {
			continue
		}

		logger(PRINT_NORMAL, message)
		cmd := Cmd{}
		err = json.Unmarshal(message, &cmd)
		if err != nil {
			log.Print("error umarshing:", err)
			return
		}
		logger(PRINT_NORMAL, cmd)
		serve_client(c, mt, cmd)
	}
}

func serve_client(c *websocket.Conn, mt int, cmd Cmd) {
	if json_cmd_translate(cmd.Command) == ADD_USER_CMD {
		logger(PRINT_NORMAL, "deviceid: ", uint64(cmd.DeviceId), "Adding new user:", cmd.Name) //TODO, get actuall id

		json_packed := db_add_employee_salary_shift(cmd, false)

		rj := json_response_packet{}
		rj.Kind = KIND_COMMAND_RESPONSE
		rj.Cmd_Type = ADD_USER_CMD
		rj.Cmd_res = json_packed

		jp, err := json.Marshal(rj)
		if err != nil {
			panic(0)
		}

		c.WriteMessage(mt, jp)
	}

	if json_cmd_translate(cmd.Command) == GET_ALL_USERS_CMD {
		logger(PRINT_NORMAL, "fetching all users from database")

		employeeArr := db_get_employees()
		logger(PRINT_NORMAL, employeeArr)
		for _, item := range employeeArr {
			rj := json_response_packet{}
			rj.Kind = KIND_GET_USER
			rj.Employee = item

			shiftArr, _ := db_get_employee_shift(item.Id)

			// Monday
			if !shiftArr.Shift_m_s.IsZero() {
				ShiftStart := shiftArr.Shift_m_s.Format(TIME_FORMAT)
				ShiftEnd := shiftArr.Shift_m_e.Format(TIME_FORMAT)
				rj.Shift_Details.Shift_m = ShiftStart + " - " + ShiftEnd
			} else {
				rj.Shift_Details.Shift_m = "-"
			}

			// Tuesday
			if !shiftArr.Shift_t_s.IsZero() {
				ShiftStart := shiftArr.Shift_t_s.Format(TIME_FORMAT)
				ShiftEnd := shiftArr.Shift_t_e.Format(TIME_FORMAT)
				rj.Shift_Details.Shift_t = ShiftStart + " - " + ShiftEnd
			} else {
				rj.Shift_Details.Shift_t = "-"
			}

			// Wendsday
			if !shiftArr.Shift_w_s.IsZero() {
				ShiftStart := shiftArr.Shift_w_s.Format(TIME_FORMAT)
				ShiftEnd := shiftArr.Shift_w_e.Format(TIME_FORMAT)
				rj.Shift_Details.Shift_w = ShiftStart + " - " + ShiftEnd
			} else {
				rj.Shift_Details.Shift_w = "-"
			}

			// Thurs
			if !shiftArr.Shift_th_s.IsZero() {
				ShiftStart := shiftArr.Shift_th_s.Format(TIME_FORMAT)
				ShiftEnd := shiftArr.Shift_th_e.Format(TIME_FORMAT)
				rj.Shift_Details.Shift_th = ShiftStart + " - " + ShiftEnd
			} else {
				rj.Shift_Details.Shift_th = "-"
			}

			// Friday
			if !shiftArr.Shift_f_s.IsZero() {
				ShiftStart := shiftArr.Shift_f_s.Format(TIME_FORMAT)
				ShiftEnd := shiftArr.Shift_f_e.Format(TIME_FORMAT)
				rj.Shift_Details.Shift_f = ShiftStart + " - " + ShiftEnd
			} else {
				rj.Shift_Details.Shift_f = "-"
			}

			jp, err := json.Marshal(rj)

			test := json_response_packet{}
			json.Unmarshal(jp, &test)
			logger(PRINT_NORMAL, test)
			if err != nil {
				panic(0)
			}
			c.WriteMessage(mt, jp)
		}
	}

	if json_cmd_translate(cmd.Command) == GET_ALL_DEVICES {
		logger(PRINT_NORMAL, "fetching all active devices!")
		deviceArr := get_all_devices()
		for _, item := range deviceArr {
			rj := json_response_packet{}
			rj.Kind = KIND_GET_DEVICE
			rj.Device = item

			jp, err := json.Marshal(rj)

			test := json_response_packet{}
			json.Unmarshal(jp, &test)
			logger(PRINT_NORMAL, test)
			if err != nil {
				panic(0)
			}
			c.WriteMessage(mt, jp)
		}
	}

	if json_cmd_translate(cmd.Command) == DELETE_SPECIFIC_USER_CMD {
		logger(PRINT_NORMAL, "deleting specifc user(s) from database")
		db_delete_user(uint32(cmd.Id))
	}

	if json_cmd_translate(cmd.Command) == UPDATE_USER_CMD {
		logger(PRINT_NORMAL, "updating specifc user(s) from database")
		json_packed := db_add_employee_salary_shift(cmd, true)

		rj := json_response_packet{}
		rj.Kind = KIND_COMMAND_RESPONSE
		rj.Cmd_Type = UPDATE_USER_CMD
		rj.Cmd_res = json_packed

		jp, err := json.Marshal(rj)
		if err != nil {
			panic(0)
		}
		c.WriteMessage(mt, jp)
	}

	if json_cmd_translate(cmd.Command) == GENERATE_REPORT {
		logger(PRINT_NORMAL, "deviceid: ", "0xdeadbeef", "Generate report", cmd.Name)

		json_packed := Cmd_resp_json{}

		/* this is an ugly hack - we will generate the report for the current period EACH time, even if it's not being requested */
		xl_create_this_period()

		fileName := xl_get_filename(cmd.Year, cmd.Month, cmd.Period)
		if exists("./static/" + fileName) {
			json_packed.Status_details = "File Exists... serving..."
			json_packed.Cmd_status = 0
		} else {
			json_packed.Status_details = "Error: Record does not exist"
			json_packed.Cmd_status = 1
		}

		rj := json_response_packet{}
		rj.Kind = KIND_COMMAND_RESPONSE
		rj.Cmd_Type = GENERATE_REPORT
		rj.Cmd_res = json_packed

		jp, err := json.Marshal(rj)
		if err != nil {
			panic(0)
		}

		c.WriteMessage(mt, jp)
	}

	if json_cmd_translate(cmd.Command) == PUSH_USER_CMD {
		logger(PRINT_NORMAL, "deviceid: ", "0xdeadbeef", "Pushing new user:", cmd.Name, "Id:", cmd.Id, "DeviceId", cmd.DeviceId)
		client := client{}
		client.deviceId = uint64(cmd.DeviceId)
		json_packed := Cmd_resp_json{}
		var rsp uint8

		if cmd.Id < 0 {
			logger(PRINT_FATAL, "RXed a negative cmd.id?!!")
		}

		employee, not_found := db_get_name_from_id(uint32(cmd.Id))
		if not_found {
			json_packed.Status_details = "Internal error - Can't find user in database"
			json_packed.Cmd_status = CMD_STATUS_FAILED
			logger(PRINT_WARN, "deviceid: ", "0xdeadbeef", "Failed to push user - no mathcing emplyee or ID = ", cmd.Id)
			goto send_response
		}

		if !check_active_devices(uint64(cmd.DeviceId)) {
			json_packed.Status_details = "Failed to push user to device, can't find device out in field! (deviceId) == " + strconv.Itoa(cmd.DeviceId)
			json_packed.Cmd_status = CMD_STATUS_FAILED
			goto send_response
		}

		if check_device_bricked(uint64(cmd.DeviceId)) != NOT_BRICKED {
			json_packed.Status_details = "Failed to push user, device is bricked with code " + strconv.Itoa(int(check_device_bricked(uint64(cmd.DeviceId))))
			json_packed.Cmd_status = CMD_STATUS_FAILED
			goto send_response
		}

		if bool(cmd.Replace) {
			logger(PRINT_NORMAL, "relpace mode set when pushing user")
		}

		rsp = add_user_to_flash_and_print(client, employee, uint32(cmd.Id), bool(cmd.Replace))

		if rsp == CMD_STATUS_GOOD {
			json_packed.Status_details = "Done Pushing User to device!"
			json_packed.Cmd_status = 0
		} else if rsp == CMD_STATUS_USER_EXISTS {
			json_packed.Status_details = "Thumbprint already exists on device!"
			json_packed.Cmd_status = CMD_STATUS_FAILED
		} else if rsp == CMD_STATUS_UID_EXISTS {
			json_packed.Status_details = "User already exists on device, check force to replace user. Note that if user fails to be added when sent with force option, the user would be deleted from the device"
			json_packed.Cmd_status = CMD_STATUS_FAILED
		} else if rsp == CMD_STATUS_FAILED_MEM_FULL {
			json_packed.Status_details = "Device is full! (try force sync?)"
			json_packed.Cmd_status = CMD_STATUS_FAILED
		} else {
			json_packed.Status_details = "Failed to add user to device!"
			json_packed.Cmd_status = CMD_STATUS_FAILED
		}

		// add to cool down timer
		if rsp == CMD_STATUS_GOOD {
			logger_id(PRINT_NORMAL, uint64(cmd.DeviceId), "Adding to cooldown period")
			cool_down_timer_mutex.Lock()
			cool_down_map[uint32(cmd.Id)] = time.Now()
			cool_down_timer_mutex.Unlock()
		}

	send_response:
		rj := json_response_packet{}
		rj.Kind = KIND_COMMAND_RESPONSE
		rj.Cmd_Type = PUSH_USER_CMD
		rj.Cmd_res = json_packed

		jp, err := json.Marshal(rj)
		if err != nil {
			panic(0)
		}

		c.WriteMessage(mt, jp)
	}

	if json_cmd_translate(cmd.Command) == FORCE_SYNC {
		sync_devices()
	}

	if json_cmd_translate(cmd.Command) == GENERATE_DEVICE_ID {
		rj := json_response_packet{}
		rj.Kind = GENERATE_DEVICE_ID
		rj.New_Device_Id = db_generate_deviceId()
		jp, err := json.Marshal(rj)
		if err != nil {
			panic(0)
		}

		c.WriteMessage(mt, jp)
	}

	if json_cmd_translate(cmd.Command) == HARD_RESET {
		os.Exit(1)
	}

	if json_cmd_translate(cmd.Command) == GET_ALL_USERS_PER_DEVICE {
		logger_id(PRINT_NORMAL, uint64(cmd.DeviceId), "deviceid: ", "Fetching all users on device:", cmd.Name, "Id:", cmd.Id)

		client := client{}
		client.deviceId = uint64(cmd.Id)
		ret, userArr, cmd_status := get_all_users(client, false)

		var at_least_one_user bool

		if ret {
			for _, emp := range userArr {

				empInfo, ok := db_get_emplyee_from_id(emp.uid)
				if !ok {
					logger_id(PRINT_NORMAL, uint64(cmd.DeviceId), "Has user in database that is no longer part of DB, needs to be synced")
					continue
				}

				at_least_one_user = true
				rj := json_response_packet{}
				rj.Kind = KIND_GET_USERS_PER_DEVICE
				rj.Employee = empInfo

				jp, err := json.Marshal(rj)
				if err != nil {
					panic(0)
				}
				c.WriteMessage(mt, jp)
			}
		}
		//final packet, send status
		rj := json_response_packet{}
		rj.Kind = GET_ALL_USERS_PER_DEVICE_FINAL /*note this is different than KIND_GET_USER_PER_DEVICE, javascript handles this */

		status := Cmd_resp_json{}
		status.Cmd_status = int(cmd_status)
		if cmd_status == CMD_STATUS_GOOD {
			status.Status_details = "Fetched all users!"
		} else if cmd_status == CMD_STATUS_FAILED_MEM_EMTPY {
			status.Status_details = "No users on device"
		} else {
			status.Status_details = "Try refreshing and trying again!? Maybe device is disconnected"
		}

		// There is at least one user on the device, but they need to be synced away since they are deleted, change response
		if cmd_status == CMD_STATUS_GOOD && !at_least_one_user {
			logger_id(PRINT_WARN, uint64(cmd.DeviceId), "Changing result to display no users")
			status.Cmd_status = CMD_STATUS_FAILED
			status.Status_details = "No users on device"
		}

		rj.Cmd_res = status
		jp, err := json.Marshal(rj)
		if err != nil {
			panic(0)
		}
		c.WriteMessage(mt, jp)

		logger_id(PRINT_NORMAL, uint64(cmd.DeviceId), "Done fetchin all users")
	}
}

func exists(name string) bool {
	if _, err := os.Stat(name); err != nil {
		if os.IsNotExist(err) {
			return false
		}
	}
	return true
}

// getUser returns a user from session s
// on error returns an empty user
func getUser(s *sessions.Session) User {
	val := s.Values["user"]
	var user = User{}
	user, ok := val.(User)
	if !ok {
		return User{Authenticated: false}
	}
	return user
}

func home(w http.ResponseWriter, req *http.Request) {
	session, err := store.Get(req, "cookie-name")
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	user := getUser(session)
	if auth := user.Authenticated; !auth {
		session.AddFlash("You don't have access!")
		err = session.Save(req, w)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		http.Redirect(w, req, "/login", http.StatusFound)
		return
	}

	t, _ := template.ParseFiles("main.html")
	t.Execute(w, "null")
}

func download(w http.ResponseWriter, req *http.Request) {
	if req.Method == "GET" {
		session, err := store.Get(req, "cookie-name")
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}

		user := getUser(session)
		if auth := user.Authenticated; !auth {
			session.AddFlash("You don't have access!")
			err = session.Save(req, w)
			if err != nil {
				http.Error(w, err.Error(), http.StatusInternalServerError)
				return
			}
			http.Redirect(w, req, "/login", http.StatusFound)
			return
		}

		req.ParseForm()
		fileName := req.Form["file"][0]
		logger(PRINT_NORMAL, "trying to serve:", fileName)
		if exists("./static/" + fileName) {
			logger(PRINT_NORMAL, "File Exists... servering:", fileName)
			w.Header().Set("Content-Disposition", "attachment; filename="+fileName)
			w.Header().Set("Content-Type", req.Header.Get("Content-Type"))
			http.ServeFile(w, req, "static/"+fileName)
		}
	}
}

func login(w http.ResponseWriter, req *http.Request) {
	if req.Method == "POST" {
		session, err := store.Get(req, "cookie-name")
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}

		req.ParseForm()
		logger(PRINT_NORMAL, req.Form)

		username := req.Form["uname"][0]
		password := req.Form["psw"][0]

		//trims excess space
		username = strings.TrimSpace(username)

		if db_site_password_verify(username, password) {
			logger(PRINT_NORMAL, "Correct password")

			user := &User{
				Username:      username,
				Authenticated: true,
			}

			session.Values["user"] = user

			err = session.Save(req, w)
			if err != nil {
				http.Error(w, err.Error(), http.StatusInternalServerError)
				return
			}
			http.Redirect(w, req, "/home", http.StatusFound)

		} else {
			logger(PRINT_NORMAL, "Incorrect password")
		}
	}
	t, _ := template.ParseFiles("microsoft.html")
	t.Execute(w, "null")
}

// sanitized the file-server not to show the "root"
// at ./static/
func neuter(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if strings.HasSuffix(r.URL.Path, "/") {
			http.NotFound(w, r)
			return
		}

		next.ServeHTTP(w, r)
	})
}

func main_site() {
	//set up session related stuff
	//TODO: this is supposed to be "secure"
	//if it is different between runs, this will mean that any cookies on the users compuer will not
	//work, hardcoded for now... but bad practice...
	encryptionKeyOne := []byte("asdaskdhasdhgsajdgasds12312hdopj")

	store = sessions.NewCookieStore(encryptionKeyOne)

	store.Options = &sessions.Options{
		MaxAge:   60 * 30, //30 minutes
		HttpOnly: true,
	}

	//what does this do?
	gob.Register(User{})

	mux := http.NewServeMux()
	mux.HandleFunc("/login", login)
	mux.HandleFunc("/home", home)
	mux.HandleFunc("/", login)
	mux.HandleFunc("/upgrade", upgrade)
	mux.HandleFunc("/download", download)

	fileServer := http.FileServer(http.Dir("./static"))
	mux.Handle("/static/", http.StripPrefix("/static", neuter(fileServer)))
	logger(PRINT_NORMAL, http.ListenAndServe(":8090", mux))
}
