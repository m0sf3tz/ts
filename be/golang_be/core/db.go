package main

import "time"
import "fmt"
import "log"
import "database/sql"
import _ "github.com/lib/pq"
import "strings"
import "os/exec"
import "strconv"
import "regexp"
import "sort"
import "math/rand"

const (
	hostname     = "localhost"
	host_port    = 5432
	username     = "postgres"
	password     = ""
	databasename = "checkin_co"
)

const UPDATE = true
const NEW_USER = false
const TIME_FORMAT_SHIFT = "15:04"
const TIME_FORMAT = "15:04:05"
const DATE_FORMAT = "2006-01-02"

var db *sql.DB

func check_and_sanitize_email(email string) (bool, string) {
	if email == "" {
		return false, ""
	}
	email = strings.ToLower(email)
	email = strings.TrimSpace(email)
	re := regexp.MustCompile(`^[a-z0-9._%+\-]+@[a-z0-9.]+\.[a-z]{2,4}$`)
	ret := re.MatchString(email)
	if ret {
		return true, email
	}
	return false, ""
}

func db_connect() {
	pg_con_string := fmt.Sprintf("host=%s port=%d user=%s  dbname=%s sslmode=disable", hostname, host_port, username, databasename)
	var err error
	db, err = sql.Open("postgres", pg_con_string)

	if err != nil {
		logger(PRINT_FATAL, "Could not connect to database!")
	}

	// We can also ping our connection which will let us know if our connection is correct /// or not then we put an error-handling code right after that.
	err = db.Ping()
	if err != nil {
		logger(PRINT_FATAL, "Could not Ping database!")
	}
}

func validateTime(start string, end string) bool {
	t_start, err := time.Parse(TIME_FORMAT_SHIFT, start)
	if err != nil {
		logger(PRINT_NORMAL, err)
		logger(PRINT_WARN, "Start time not a real time")
		return false
	}

	t_end, err := time.Parse(TIME_FORMAT_SHIFT, end)
	if err != nil {
		logger(PRINT_NORMAL, err)
		logger(PRINT_WARN, "End time not a real time")
		return false
	}

	if t_start.After(t_end) || (t_start == t_end) {
		logger(PRINT_WARN, "Start time is later than end time")
		return false
	}

	return true
}

func db_add_employee_salary_shift(cmd Cmd, update bool) Cmd_resp_json {
	ret := Cmd_resp_json{}

	if !update {
		if cmd.Name == "" {
			logger(PRINT_WARN, "nil name recived!")
			ret.Cmd_status = CMD_STATUS_FAILED
			ret.Status_details = "Nill name RXed"
			return ret
		}
	}

	if cmd.Salary == "" {
		logger(PRINT_WARN, "Salary none")
		ret.Cmd_status = CMD_STATUS_FAILED
		ret.Status_details = "Salaray null!"
		return ret
	}

	salary_f, err := strconv.ParseFloat(cmd.Salary, 64)

	if 0 > salary_f || salary_f > 200000 {
		logger(PRINT_WARN, "Inputs seem out of range.... for salaray:", salary_f)
		ret.Cmd_status = CMD_STATUS_FAILED
		ret.Status_details = "Saray too high"
		return ret
	}

	valid_email, cleaned_email := check_and_sanitize_email(cmd.Email)

	if !valid_email {
		logger(PRINT_WARN, "Email is wrong:", cmd.Email)
		ret.Cmd_status = CMD_STATUS_FAILED
		ret.Status_details = "Invalid Email"
		return ret
	}

	row := db.QueryRow(`select id from employeeinfo where email=$1;`, cleaned_email)
	if err != nil {
		log.Fatal(err)
	}

	var id int
	switch err := row.Scan(&id); err {
	case sql.ErrNoRows:
		logger(PRINT_NORMAL, "Adding User email %s", cmd.Email)
		break
	case nil:
		logger(PRINT_WARN, "Email already in database:", cmd.Email)
		ret.Cmd_status = CMD_STATUS_FAILED
		ret.Status_details = "Email Exists in database (Email must be unique!)"
		return ret
		logger(PRINT_NORMAL, id)
	default:
		logger(PRINT_FATAL, "Could not add user email!")
	}

	row = db.QueryRow(`select id from employeeinfo where employee=$1;`, cmd.Name)
	if err != nil {
		log.Fatal(err)
	}

	switch err := row.Scan(&id); err {
	case sql.ErrNoRows:
		logger(PRINT_NORMAL, "Adding User Name %s", cmd.Name)
		break
	case nil:
		logger(PRINT_WARN, "Name already in database:", cmd.Name)
		ret.Cmd_status = CMD_STATUS_FAILED
		ret.Status_details = "Name exists in database!, must be unique!"
		return ret
		logger(PRINT_NORMAL, id)
	default:
		logger(PRINT_FATAL, "Could not add user name!")
	}

	if update {
		/* updating user, make sure ID exists in databse */
		row := db.QueryRow(`select id from employeeinfo where id=$1;`, cmd.Id)
		if err != nil {
			log.Fatal(err)
		}

		var id int
		switch err := row.Scan(&id); err {
		case sql.ErrNoRows:
			logger(PRINT_WARN, "Tried to update user with no matching id", cmd.Id)
			ret.Cmd_status = CMD_STATUS_FAILED
			ret.Status_details = "Id does not exist in database!"
			return ret
			break
		case nil:
			logger(PRINT_NORMAL, "Updating user Id = ", cmd.Id)
		default:
			logger(PRINT_FATAL, "Could not update user! ", cmd.Id)
		}
	}

	if cmd.Shift_m_s == "" && cmd.Shift_t_s == "" && cmd.Shift_w_s == "" && cmd.Shift_th_s == "" && cmd.Shift_f_e == "" {
		logger(PRINT_WARN, "No shift info RXed:")
		ret.Cmd_status = CMD_STATUS_FAILED
		ret.Status_details = "No Shift info!"
		return ret
	}

	if cmd.Shift_m_s != "" && cmd.Shift_m_e != "" {
		if !validateTime(cmd.Shift_m_s, cmd.Shift_m_e) {
			ret.Cmd_status = CMD_STATUS_FAILED
			ret.Status_details = "Please correct Monday shift start/end"
			return ret
		}
	}

	if cmd.Shift_t_s != "" && cmd.Shift_t_e != "" {
		if !validateTime(cmd.Shift_t_s, cmd.Shift_t_e) {
			ret.Cmd_status = CMD_STATUS_FAILED
			ret.Status_details = "Please correct Tuesday shift start/end"
			return ret
		}
	}

	if cmd.Shift_w_s != "" && cmd.Shift_w_e != "" {
		if !validateTime(cmd.Shift_w_s, cmd.Shift_w_e) {
			ret.Cmd_status = CMD_STATUS_FAILED
			ret.Status_details = "Please correct Wendaday shift start/end"
			return ret
		}
	}

	if cmd.Shift_th_s != "" && cmd.Shift_th_e != "" {
		if !validateTime(cmd.Shift_th_s, cmd.Shift_th_e) {
			ret.Cmd_status = CMD_STATUS_FAILED
			ret.Status_details = "Please correct Thursday shift start/end"
			return ret
		}
	}

	if cmd.Shift_f_s != "" && cmd.Shift_f_e != "" {
		if !validateTime(cmd.Shift_f_s, cmd.Shift_f_e) {
			ret.Cmd_status = CMD_STATUS_FAILED
			ret.Status_details = "Please correct Friday shift start/end"
			return ret
		}
	}

	if update { //updating an existing user
		_, err = db.Exec(`UPDATE employeeinfo SET salary = $1, email = $2 where id = $3`, salary_f, cleaned_email, cmd.Id)
		if err != nil {
			log.Fatal(err)
		}

		_, err = db.Exec(`UPDATE employeeinfo SET  shift_m_s = NULL, shift_m_e = NULL where id = $1`, cmd.Id)
		if err != nil {
			log.Fatal(err)
		}
		_, err = db.Exec(`UPDATE employeeinfo SET  shift_t_s = NULL, shift_t_e = NULL where id = $1`, cmd.Id)
		if err != nil {
			log.Fatal(err)
		}
		_, err = db.Exec(`UPDATE employeeinfo SET  shift_w_s = NULL, shift_w_e = NULL where id = $1`, cmd.Id)
		if err != nil {
			log.Fatal(err)
		}
		_, err = db.Exec(`UPDATE employeeinfo SET  shift_th_s = NULL, shift_th_e = NULL where id = $1`, cmd.Id)
		if err != nil {
			log.Fatal(err)
		}
		_, err = db.Exec(`UPDATE employeeinfo SET  shift_f_s = NULL, shift_f_e = NULL where id = $1`, cmd.Id)
		if err != nil {
			log.Fatal(err)
		}

		if cmd.Shift_m_s != "" && cmd.Shift_m_e != "" {
			_, err = db.Exec(`UPDATE employeeinfo SET shift_m_s = $1, shift_m_e = $2 where id=$3`, cmd.Shift_m_s, cmd.Shift_m_e, cmd.Id)
			if err != nil {
				log.Fatal(err)
			}
		}

		if cmd.Shift_t_s != "" && cmd.Shift_t_e != "" {
			_, err = db.Exec(`UPDATE employeeinfo SET shift_t_s = $1, shift_t_e = $2 where id=$3`, cmd.Shift_t_s, cmd.Shift_t_e, cmd.Id)
			if err != nil {
				log.Fatal(err)
			}
		}

		if cmd.Shift_w_s != "" && cmd.Shift_w_e != "" {
			_, err = db.Exec(`UPDATE employeeinfo SET shift_w_s = $1, shift_w_e = $2 where id=$3`, cmd.Shift_w_s, cmd.Shift_w_e, cmd.Id)
			if err != nil {
				log.Fatal(err)
			}
		}

		if cmd.Shift_th_s != "" && cmd.Shift_th_e != "" {
			_, err = db.Exec(`UPDATE employeeinfo SET shift_th_s = $1, shift_th_e = $2 where id=$3`, cmd.Shift_th_s, cmd.Shift_th_e, cmd.Id)
			if err != nil {
				log.Fatal(err)
			}
		}

		if cmd.Shift_f_s != "" && cmd.Shift_f_e != "" {
			_, err = db.Exec(`UPDATE employeeinfo SET shift_f_s = $1, shift_f_e = $2 where id=$3`, cmd.Shift_f_s, cmd.Shift_f_e, cmd.Id)
			if err != nil {
				log.Fatal(err)
			}
		}
	} else { //New user

		cmd.Name = strings.TrimSpace(cmd.Name)
		_, err = db.Exec(`INSERT INTO employeeinfo(employee, salary, email) VALUES ($1, $2, $3)`, cmd.Name, salary_f, cleaned_email)
		if err != nil {
			log.Fatal(err)
		}

		if cmd.Shift_m_s != "" && cmd.Shift_m_e != "" {
			_, err = db.Exec(`UPDATE employeeinfo SET shift_m_s = $1, shift_m_e = $2 where email=$3`, cmd.Shift_m_s, cmd.Shift_m_e, cleaned_email)
			if err != nil {
				log.Fatal(err)
			}
		}

		if cmd.Shift_t_s != "" && cmd.Shift_t_e != "" {
			_, err = db.Exec(`UPDATE employeeinfo SET shift_t_s = $1, shift_t_e = $2 where email=$3`, cmd.Shift_t_s, cmd.Shift_t_e, cleaned_email)
			if err != nil {
				log.Fatal(err)
			}
		}

		if cmd.Shift_w_s != "" && cmd.Shift_w_e != "" {
			_, err = db.Exec(`UPDATE employeeinfo SET shift_w_s = $1, shift_w_e = $2 where email=$3`, cmd.Shift_w_s, cmd.Shift_w_e, cleaned_email)
			if err != nil {
				log.Fatal(err)
			}
		}

		if cmd.Shift_th_s != "" && cmd.Shift_th_e != "" {
			_, err = db.Exec(`UPDATE employeeinfo SET shift_th_s = $1, shift_th_e = $2 where email=$3`, cmd.Shift_th_s, cmd.Shift_th_e, cleaned_email)
			if err != nil {
				log.Fatal(err)
			}
		}

		if cmd.Shift_f_s != "" && cmd.Shift_f_e != "" {
			_, err = db.Exec(`UPDATE employeeinfo SET shift_f_s = $1, shift_f_e = $2 where email=$3`, cmd.Shift_f_s, cmd.Shift_f_e, cleaned_email)
			if err != nil {
				log.Fatal(err)
			}
		}
	}

	return ret
}

func db_put(deviceId uint64, signInOrSignOut uint8, uid uint32, Time time.Time) bool {
	var signInOrSignOut_string string

	emp, err := db_get_name_from_id(uid)
	if err {
		logger_id(PRINT_WARN, deviceId, "uid", uid, "has no matching employee?") //TODO: send a warning to the device?
		return false
	}

	if signInOrSignOut == PARALLAX_SIGN_IN {
		logger_id(PRINT_NORMAL, deviceId, "uid", uid, "Just signed in")
		signInOrSignOut_string = "login"
	} else {
		logger_id(PRINT_NORMAL, deviceId, "uid", uid, "Just signed out")
		signInOrSignOut_string = "logout"
	}

	var current_time_local time.Time
	var current_date, current_time string

	/* if not given a time, use the current time */
	if Time.IsZero() {
		current_time_local = time.Now().Local()
		current_date = current_time_local.Format("2006-01-02")
		current_time = current_time_local.Format("15:4:5")
	} else { /*Fake current time, used for testing */
		current_time_local = Time
		current_date = current_time_local.Format("2006-01-02")
		current_time = current_time_local.Format("15:4:5")
	}

	logger_id(PRINT_NORMAL, deviceId, "uid = ", uid, "date=", current_date, "signInOrSignOut=", signInOrSignOut_string, "current_time=", current_time)
	_, err2 := db.Exec("INSERT INTO timeinfo VALUES ($1, $2, $3, $4, $5);", uid, emp, current_date, signInOrSignOut_string, current_time)
	if err2 != nil {
		log.Fatal(err)
	}
	return true
}

func db_get() []db_q_fill {
	var ret []db_q_fill
	r := db_q_fill{}

	rows, err := db.Query("select * from login")
	if err != nil {
		log.Fatal(err)
	}
	defer rows.Close()
	for rows.Next() {
		err := rows.Scan(&r.Temperature, &r.Time, &r.User_name)
		if err != nil {
			log.Fatal(err)
		}
		ret = append(ret, r)
	}
	err = rows.Err()
	if err != nil {
		log.Fatal(err)
	}
	return ret
}

func db_delete_user(id uint32) bool {
	row := db.QueryRow(`select id from employeeinfo where id=$1;`, id)

	var id_valid int
	switch err := row.Scan(&id_valid); err {
	case sql.ErrNoRows:
		logger(PRINT_NORMAL, "User not in database! can't delete", id)
		return false
		break
	case nil:
		logger(PRINT_NORMAL, "User in database!, will delete", id)
	default:
		logger(PRINT_FATAL, "Could not delete user from db!", id)
	}

	_, err := db.Exec(`DELETE FROM employeeinfo WHERE id=$1`, id)
	if err != nil {
		log.Fatal(err)
	}
	return true
}

func db_get_salary(id uint32) (int, bool) {
	row := db.QueryRow(`select salary from employeeinfo where id=$1;`, id)

	var Salary int

	switch err := row.Scan(&Salary); err {
	case sql.ErrNoRows:
		logger(PRINT_WARN, "No matching ID for this username", id)
		return 0, false
	case nil:
		return Salary, true
	default:
		logger(PRINT_FATAL, "Could not get user salary from db!", id)
		return 0, false
	}
}

func db_get_name_from_id(id uint32) (string, bool) {
	logger(PRINT_NORMAL, "trying to match id = ", id, ", to user!")

	row := db.QueryRow(`select employee from employeeinfo where id=$1;`, id)

	var employee string
	switch err := row.Scan(&employee); err {
	case sql.ErrNoRows:
		logger(PRINT_WARN, "No matching ID for this username", id)
		return "", true
	case nil:
		return employee, false
	default:
		logger(PRINT_FATAL, "Could not get user name from db (from ID)!", id)
		return "", false
	}
}

func db_get_employees() []db_get_employee_info {
	var ret []db_get_employee_info
	r := db_get_employee_info{}

	rows, err := db.Query("select id, email, employee, Salary from employeeinfo")
	if err != nil {
		logger(PRINT_FATAL, "failed in initial quary #1, db_get_employees", err)
	}
	defer rows.Close()
	for rows.Next() {
		err := rows.Scan(&r.Id, &r.Email, &r.Name, &r.Salary)
		if err != nil {
			log.Fatal(err)
		}
		ret = append(ret, r)
	}
	err = rows.Err()
	if err != nil {
		logger(PRINT_FATAL, "failed in final quary #2, db_get_employees", err)
	}
	return ret
}

func db_get_employee_shift_day(id uint32, day time.Weekday) (db_single_shift, bool) {
	info, valid := db_get_employee_shift(id)
	ret := db_single_shift{}
	if valid {
		if day == 1 {
			ret.Shift_start = info.Shift_m_s
			ret.Shift_end = info.Shift_m_e
			return ret, true
		}
		if day == 2 {
			ret.Shift_start = info.Shift_t_s
			ret.Shift_end = info.Shift_t_e
			return ret, true
		}
		if day == 3 {
			ret.Shift_start = info.Shift_w_s
			ret.Shift_end = info.Shift_w_e
			return ret, true
		}
		if day == 4 {
			ret.Shift_start = info.Shift_th_s
			ret.Shift_end = info.Shift_th_e
			return ret, true
		}
		if day == 5 {
			ret.Shift_start = info.Shift_f_s
			ret.Shift_end = info.Shift_f_e
			return ret, true
		}
	}
	logger(PRINT_WARN, "Checking work times on the weekend?")
	return db_single_shift{}, false
}

func db_get_employee_shift(id uint32) (Shift_info, bool) {
	var ret Shift_info_sql
	var retreal Shift_info

	row := db.QueryRow(`select shift_m_s, shift_m_e,
 											       shift_t_s, shift_t_e,
														 shift_w_s, shift_w_e,
														 shift_th_s, shift_th_e,
														 shift_f_s, shift_f_e
														 from employeeinfo where id = $1`, id)

	switch err := row.Scan(&ret.Shift_m_s, &ret.Shift_m_e,
		&ret.Shift_t_s, &ret.Shift_t_e,
		&ret.Shift_w_s, &ret.Shift_w_e,
		&ret.Shift_th_s, &ret.Shift_th_e,
		&ret.Shift_f_s, &ret.Shift_f_e); err {
	case sql.ErrNoRows:
		logger(PRINT_WARN, "No such user exists with id = ", id)
		return retreal, false
		break
	case nil:
	default:
		logger(PRINT_FATAL, "Could not get user shift infro from ID ", id)
	}

	if ret.Shift_m_s.Valid && ret.Shift_m_e.Valid {
		retreal.Shift_m_s = ret.Shift_m_s.Time
		retreal.Shift_m_e = ret.Shift_m_e.Time
	}

	if ret.Shift_t_s.Valid && ret.Shift_t_e.Valid {
		retreal.Shift_t_s = ret.Shift_t_s.Time
		retreal.Shift_t_e = ret.Shift_t_e.Time
	}

	if ret.Shift_w_s.Valid && ret.Shift_w_e.Valid {
		retreal.Shift_w_s = ret.Shift_w_s.Time
		retreal.Shift_w_e = ret.Shift_w_e.Time
	}

	if ret.Shift_th_s.Valid && ret.Shift_th_e.Valid {
		retreal.Shift_th_s = ret.Shift_th_s.Time
		retreal.Shift_th_e = ret.Shift_th_e.Time
	}

	if ret.Shift_f_s.Valid && ret.Shift_f_e.Valid {
		retreal.Shift_f_s = ret.Shift_f_s.Time
		retreal.Shift_f_e = ret.Shift_f_e.Time
	}

	return retreal, true
}

func db_get_emplyee_from_id(id uint32) (db_get_employee_info, bool) {
	logger(PRINT_NORMAL, "Fetching emplyee with id =", id)

	var ret db_get_employee_info

	row := db.QueryRow(`select id, email, employee from employeeinfo where id = $1`, id)

	switch err := row.Scan(&ret.Id, &ret.Email, &ret.Name); err {
	case sql.ErrNoRows:
		logger(PRINT_WARN, "No such user exists with id = ", id)
		return ret, false
		break
	case nil:
	default:
		logger(PRINT_FATAL, "Could not get user employee from ID ", id)
	}
	return ret, true
}

// If an emplyee is supposed to work on a day, their work will be judged versus their expted shift */
// if they
// 	A) Are supposed to work and,
//      1) login, logout in a sensible manner, work time is returned
//      2) forget to do either login, or logout, logout/login time that is missing will be taken from shift dabatase and it will be flagged
//			3) not login AND logout, 0 will be retuend
//  b) are not supposed to work and,
//			1)  login, logout in a sensible manner, work time is returned, but this is flagged
//      2)  missing eitehr log in or logout time, flagged as error, 0 work time returned
//  c) worked a negative amount of time, (logged in befoer they logged out), this is flagged as an error
//  d) Did no work on a day they are not uspposed to wrok
//     1 ) 0 time is returned, and it is "flagged"
//  for multiple login/logout cases, the earliest login is considered and the latest logout
//
func db_get_time_worked_in_day(id uint32, Time time.Time) (time.Duration, int, db_single_shift, db_single_shift, int) {
	if Time.Weekday() == 0 || Time.Weekday() == 6 {
		logger(PRINT_WARN, "Tried to check work hours on weekend, not-supported")
		return 0, 0, db_single_shift{}, db_single_shift{}, 0
	}

	_, exists := db_get_emplyee_from_id(id)
	if !exists {
		logger(PRINT_FATAL, "Tried to quary hours worked for emplyee that does not exist!", id)
	}

	date := Time.Format(DATE_FORMAT)

	var time_info_login []time.Time
	var time_info_logout []time.Time
	var time_info time.Time

	rows, err := db.Query(`select timestamp from timeinfo where id = $1 and datework = $2 and type = 'login'`, id, date)
	if err != nil {
		log.Fatal(err)
	}
	defer rows.Close()
	for rows.Next() {
		err := rows.Scan(&time_info)
		if err != nil {
			logger(PRINT_FATAL, "Failed fetching logins - loop")
		}
		time_info_login = append(time_info_login, time_info)
	}
	err = rows.Err()
	if err != nil {
		logger(PRINT_FATAL, "Failed fetching logins")
	}

	rows, err = db.Query(`select timestamp from timeinfo where id = $1 and datework = $2 and type = 'logout'`, id, date)
	if err != nil {
		log.Fatal(err)
	}
	defer rows.Close()
	for rows.Next() {
		err := rows.Scan(&time_info)
		if err != nil {
			logger(PRINT_FATAL, "Failed fetching logout - loop")
		}
		time_info_logout = append(time_info_logout, time_info)
	}
	err = rows.Err()
	if err != nil {
		logger(PRINT_FATAL, "Failed fetching logouts")
	}

	shift, valid := db_get_employee_shift_day(id, Time.Weekday())
	if !valid {
		logger(PRINT_FATAL, "Invalid shift returned")
	}

	/* used to let other APIs know if a login/logout was taking from shift
	infromation instead of real values */
	var missing_login_logout int

	/* no login AND logout times  */
	if len(time_info_login) == 0 && len(time_info_logout) == 0 {
		/* not supposed to work today */
		if shift.Shift_start.IsZero() {
			return 0, TIME_WORKED_VALID, db_single_shift{}, db_single_shift{}, 0
		}
		return 0, TIME_WORKED_MISSING, db_single_shift{}, db_single_shift{}, 0
	}

	var ret int

	/* worked on wrong day, mark it as such */
	if shift.Shift_start.IsZero() {
		ret = TIME_WORKED_ON_WRONG_DAY
	}

	if len(time_info_login) == 0 {
		if shift.Shift_start.IsZero() {
			ret = TIME_WORKED_MISSING_ONE_ON_NON_WORK_DAY
			goto calculate_time
		}
		missing_login_logout = TIME_MISSING_LOGIN
		time_info_login = append(time_info_login, shift.Shift_start)
		ret = TIME_WORKED_MISSING_ONE
	}

	if len(time_info_logout) == 0 {
		if shift.Shift_end.IsZero() {
			ret = TIME_WORKED_MISSING_ONE_ON_NON_WORK_DAY
			goto calculate_time
		}
		missing_login_logout = TIME_MISSING_LOGOUT
		time_info_logout = append(time_info_logout, shift.Shift_end)
		ret = TIME_WORKED_MISSING_ONE
	}

calculate_time:
	var first_in, last_out time.Time

	if len(time_info_login) > 0 {
		first_in = time_info_login[0]
		for _, login := range time_info_login {
			if login.Before(first_in) {
				first_in = login
			}
		}
	}

	if len(time_info_logout) > 0 {
		last_out = time_info_logout[0]
		for _, logout := range time_info_logout {
			if logout.After(last_out) {
				last_out = logout
			}
		}
	}

	total_work := last_out.Sub(first_in)

	if ret != TIME_WORKED_MISSING_ONE_ON_NON_WORK_DAY {
		if total_work < 0 {
			return 0, TIME_WORKED_NEGATIVE, db_single_shift{}, db_single_shift{}, 0
		}
	}

	/* actuall shift details */
	var shift_actual, shift_expected db_single_shift
	shift_actual.Shift_start = first_in
	shift_actual.Shift_end = last_out

	shift_expected.Shift_start = shift.Shift_start
	shift_expected.Shift_end = shift.Shift_end

	if ret == TIME_WORKED_MISSING_ONE_ON_NON_WORK_DAY {
		total_work = 0
	}
	return total_work, ret, shift_expected, shift_actual, missing_login_logout
}

func db_get_shift_duration_weekday(id uint32, Time time.Time) time.Duration {
	shift, err := db_get_employee_shift_day(id, Time.Weekday())
	if !err {
		logger(PRINT_FATAL, "tried to quarry during weekend")
	}
	return shift.Shift_end.Sub(shift.Shift_start)
}

func db_generate_deviceId() int {
	logger(PRINT_NORMAL, "Selecteding new deviceId")

	var randomdeviceId int32
	for {
		randomdeviceId = rand.Int31n(0x7FFFFFF)

		row := db.QueryRow(`select deviceId from registeredDeviceId where deviceId=$1;`, randomdeviceId)

		var deviceid int
		switch err := row.Scan(&deviceid); err {
		case sql.ErrNoRows:
			logger(PRINT_NORMAL, "DeviceId don't exists")
			goto exit
		case nil:
			logger(PRINT_NORMAL, "DeviceId exists")
			continue
		default:
			logger(PRINT_FATAL, "Failed to generate a random deviceId")
		}
	}
exit:
	_, err := db.Exec(`INSERT INTO registeredDeviceId(deviceid) VALUES ($1)`, randomdeviceId)
	if err != nil {
		logger(PRINT_FATAL, "Failed to insert new registered device id with err", err)
	}

	logger(PRINT_NORMAL, "Selected as new deviceId", randomdeviceId)
	return int(randomdeviceId)
}

func db_truncate_timeinfo() {
	_, err := db.Exec(`TRUNCATE timeinfo`)
	if err != nil {
		logger(PRINT_FATAL, "Failed to truncate time info", err)
	}
}

func db_truncate_employeeifo() {
	_, err := db.Exec(`TRUNCATE employeeinfo`)
	if err != nil {
		logger(PRINT_FATAL, "Failed to truncate employee info", err)
	}
}

func db_get_pay_period_arr(id uint32, Time time.Time) []db_composite_per_day {
	if Time.Day() != 1 && Time.Day() != 16 {
		logger(PRINT_FATAL, "Can only accept on 16 and 1st day of month")
	}
	var lastDate int
	var ret []db_composite_per_day
	var day db_composite_per_day
	var totalDaysInPeriod int

	if Time.Day() == 1 {
		totalDaysInPeriod = 15
	} else { /*magic to calculate last day of the month */
		firstDate := Time.AddDate(0, 0, -15)
		lastDate = firstDate.AddDate(0, 1, -1).Day()
		totalDaysInPeriod = lastDate - 15
	}

	for i := 0; i < totalDaysInPeriod; i++ {
		if Time.AddDate(0, 0, i).Weekday() == 0 || Time.AddDate(0, 0, i).Weekday() == 6 {
			continue
		}
		total_worked, valid, expected_shift, actual_shift, missing := db_get_time_worked_in_day(id, Time.AddDate(0, 0, i))
		shift_duration_expected := db_get_shift_duration_weekday(id, Time.AddDate(0, 0, i))

		day.date = Time.AddDate(0, 0, i)
		day.actual_shift = actual_shift
		day.expected_shift = expected_shift
		day.actual_hours = total_worked
		day.anomolies = valid
		day.expected_hours = shift_duration_expected

		if missing == TIME_MISSING_LOGIN {
			day.actual_shift.Shift_start = time.Time{}
		}

		if missing == TIME_MISSING_LOGOUT {
			day.actual_shift.Shift_end = time.Time{}
		}

		if missing == TIME_MISSING_LOGIN || missing == TIME_MISSING_LOGOUT {
			logger(PRINT_NORMAL, day.actual_shift, "here!!")
		}

		day.delta = day.expected_hours - day.actual_hours
		ret = append(ret, day)
	}

	return ret

}

func db_get_total_hours_in_period(id uint32, Time time.Time) db_hours_pay_period {
	if Time.Day() != 1 && Time.Day() != 16 {
		logger(PRINT_FATAL, "Can only accept on 16 and 1st day of month")
	}
	var lastDate int
	var ret db_hours_pay_period

	startDate := Time.Day()

	if Time.Day() == 1 {
		lastDate = 15
	} else {
		//magic to calculate last day of the month

		//goto first date
		Time = Time.AddDate(0, 0, -15)

		//add 1 month, subtract 1 day
		lastDate = Time.AddDate(0, 1, -1).Day()
	}

	for i := startDate; i <= lastDate; i++ {
		if Time.AddDate(0, 0, i).Weekday() == 0 || Time.AddDate(0, 0, i).Weekday() == 6 {
			continue
		}
		total_worked, valid, _, _, _ := db_get_time_worked_in_day(id, Time.AddDate(0, 0, i))
		shift_duration := db_get_shift_duration_weekday(id, Time.AddDate(0, 0, i))

		ret.actual_hours += total_worked
		if valid != TIME_WORKED_MISSING_ONE_ON_NON_WORK_DAY {
			ret.expected_hours += shift_duration
		}

		if valid != 0 {
			ret.anomolies = true
		}
	}
	hourly_wage, valid := db_get_employee_hourly_salary(id)
	if valid {
		ret.exptected_sal = ret.expected_hours.Hours() * hourly_wage
		ret.actual_sal = ret.actual_hours.Hours() * hourly_wage
	}
	return ret
}

func db_get_anomoly_string(n int) string {
	switch n {
	case TIME_WORKED_VALID:
		return ""
	case TIME_WORKED_MISSING_ONE:
		return "login or logout missing"
	case TIME_WORKED_MISSING:
		return "Both login AND logout missing"
	case TIME_WORKED_NEGATIVE:
		return "Time work negative"
	case TIME_WORKED_MISSING_ONE_ON_NON_WORK_DAY:
		return "Not a workday but worked, missing either login or logout"
	case TIME_WORKED_ON_WRONG_DAY:
		return "Worked on wrong day"
	default:
		logger(PRINT_FATAL, "Failed to get anomoly string!")
		return ""
	}
}

func db_get_employee_hourly_salary(id uint32) (float64, bool) {
	var dur time.Duration
	for i := 1; i < 6; i++ {
		shift, _ := db_get_employee_shift_day(id, time.Weekday(i))
		dur += shift.Shift_end.Sub(shift.Shift_start)
	}

	/* this is yearly */
	salery, err := db_get_salary(id)
	if !err {
		return 0, false
	}

	if dur == 0 {
		return 0, false
	}
	return float64(salery) / (52 * dur.Hours()), true
}

func db_sync_users(emp_list []employee) (bool, []byte) {
	logger(PRINT_NORMAL, "here")

	ret := make([]byte, MAX_USERS_DEVICE)

	for _, employee := range emp_list {
		row := db.QueryRow(`select id from employeeinfo where id=$1;`, employee.uid)

		var id int
		switch err := row.Scan(&id); err {
		case sql.ErrNoRows:
			logger(PRINT_NORMAL, "User don't exists= ", employee.id, employee.name)
			ret[employee.id] = SYNC_DELETE_USER_BIT_FIELD
			break
		case nil:
			logger(PRINT_NORMAL, "User still exists= ", employee.id, employee.name)
			ret[employee.id] = SYNC_USER_EXISTS_BIT_FIELD
		default:
			logger(PRINT_FATAL, "Failed to sync DB's...!")
		}
	}
	return true, ret
}

/* for test */
func db_sync_compare(emp_arr_dev []employee) bool {
	emp_arr_db := db_get_employees()

	if len(emp_arr_dev) != len(emp_arr_db) {
		return false
	}

	sort.Slice(emp_arr_db, func(i, j int) bool {
		return emp_arr_db[i].Id < emp_arr_db[j].Id
	})

	sort.Slice(emp_arr_dev, func(i, j int) bool {
		return emp_arr_dev[i].uid < emp_arr_dev[j].uid
	})

	for i := 0; i < len(emp_arr_db); i++ {
		if emp_arr_db[i].Id != emp_arr_dev[i].uid {
			logger(PRINT_NORMAL, "DB!")
			for _, v := range emp_arr_db {
				logger(PRINT_NORMAL, v)
			}
			logger(PRINT_NORMAL, "device!")
			for _, v := range emp_arr_dev {
				logger(PRINT_NORMAL, v)
			}
			logger(PRINT_FATAL, "Failed to do a db_sync_compare...!")
		}
	}
	return true
}

func db_site_password_verify(user_name, password string) bool {
	logger(PRINT_NORMAL, "Verifying password for user:", user_name, "with password:", password)

	row := db.QueryRow(`select password from sitepasswords where username = $1`, user_name)

	var password_db string

	switch err := row.Scan(&password_db); err {
	case sql.ErrNoRows:
		logger(PRINT_WARN, "No such user exists with userName= ", user_name)
		return false
	case nil:
		logger(PRINT_NORMAL, "User exists!")
		break
	default:
		logger(PRINT_FATAL, "Failed to verify site password")
	}

	//if we got here that means user name exists
	if password_db == password {
		logger(PRINT_NORMAL, "Password is good!")
		return true
	} else {
		logger(PRINT_NORMAL, "Password is bad!")
		return false
	}
}

func db_export_csv() {
	cmd := exec.Command(">output.csv")
	cmd.Dir = "./static"
	cmd.Run()

	str := fmt.Sprintf("COPY timeinfo TO '/usr/local/timeScan/core/static/output.csv' WITH (FORMAT CSV, HEADER);")
	_, err := db.Exec(str)
	if err != nil {
		log.Fatal(err)
	}
}
