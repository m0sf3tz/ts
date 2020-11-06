package main

import (
	"database/sql"
	"time"
)

// These are shared with JS, used to select what kind of websocket just
// got sent
const KIND_GET_USER = 0
const NOT_USED_0 = 1
const KIND_COMMAND_RESPONSE = 2
const KIND_GET_DEVICE = 3
const KIND_GET_USERS_PER_DEVICE = 4

type User struct {
	Username      string
	Authenticated bool
}

type deviceSite struct {
	Fw_version  uint16
	DeviceId    uint64
	Device_name string
	Bricked     uint8
	UpTime      string
}

type json_response_packet struct {
	Kind          int
	Cmd_Type      int // if a kind is a command, which command is it?
	Cmd_res       Cmd_resp_json
	Employee      db_get_employee_info
	Device        deviceSite
	New_Device_Id int
	Shift_Details Shift_Info_String
}

type site_login_json struct {
	Temperature string
	User_name   string
	Time        string
}

type Cmd struct {
	Command    string
	Name       string
	Salary     string
	Email      string
	Id         int
	Shift_m_s  string
	Shift_m_e  string
	Shift_t_s  string
	Shift_t_e  string
	Shift_w_s  string
	Shift_w_e  string
	Shift_th_s string
	Shift_th_e string
	Shift_f_s  string
	Shift_f_e  string
	Year       int
	Month      int
	Period     int
	DeviceId   int
	Replace    bool
}

type Shift_info struct {
	Shift_m_s  time.Time
	Shift_m_e  time.Time
	Shift_t_s  time.Time
	Shift_t_e  time.Time
	Shift_w_s  time.Time
	Shift_w_e  time.Time
	Shift_th_s time.Time
	Shift_th_e time.Time
	Shift_f_s  time.Time
	Shift_f_e  time.Time
}

/* this is used to quarry SQL, since we can have NULL-times*/
type Shift_info_sql struct {
	Shift_m_s  sql.NullTime
	Shift_m_e  sql.NullTime
	Shift_t_s  sql.NullTime
	Shift_t_e  sql.NullTime
	Shift_w_s  sql.NullTime
	Shift_w_e  sql.NullTime
	Shift_th_s sql.NullTime
	Shift_th_e sql.NullTime
	Shift_f_s  sql.NullTime
	Shift_f_e  sql.NullTime
}

type Shift_Info_String struct {
	Shift_m  string
	Shift_t  string
	Shift_w  string
	Shift_th string
	Shift_f  string
}

type db_get_employee_info struct {
	Name   string
	Id     uint32
	Email  string
	Salary int
}

type db_emplyee_hours_worked struct {
	Time time.Time
	Type string
}

type Cmd_resp_json struct {
	Cmd_status     int
	Status_details string
}

type db_single_shift struct {
	Shift_start time.Time
	Shift_end   time.Time
}

type db_hours_pay_period struct {
	actual_hours   time.Duration
	expected_hours time.Duration
	anomolies      bool
	exptected_sal  float64
	actual_sal     float64
}

type db_composite_per_day struct {
	date           time.Time
	actual_shift   db_single_shift
	expected_shift db_single_shift
	actual_hours   time.Duration
	expected_hours time.Duration
	delta          time.Duration
	anomolies      int
}

const TIME_WORKED_VALID = 0
const TIME_WORKED_MISSING_ONE = 1 /*login XOR logout time missing */
const TIME_WORKED_MISSING = 2     /*both login+logout times missing */
const TIME_WORKED_NEGATIVE = 3
const TIME_WORKED_MISSING_ONE_ON_NON_WORK_DAY = 4
const TIME_WORKED_ON_WRONG_DAY = 5

const TIME_MISSING_NONE = 0
const TIME_MISSING_LOGIN = 1
const TIME_MISSING_LOGOUT = 2
const TIME_MISSING_BOTH = 3
