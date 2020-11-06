package main

import "github.com/Pallinder/go-randomdata"
import "fmt"
import "time"
import "math/rand"

func test_add_employee() {
	test_cmd := Cmd{}
	test_cmd.Name = randomdata.FullName(randomdata.RandomGender)
	test_cmd.Email = randomdata.Email()
	test_cmd.Salary = "50000"

	if rand.Int31n(2) == 1 {
		test_cmd.Shift_m_s = time.Date(2000, 1, 1, 7, 0, 0, 0, time.UTC).Add(time.Second * 0 * time.Duration(rand.Int31n(3600*2)-3600)).Format(TIME_FORMAT_SHIFT)
		test_cmd.Shift_m_e = time.Date(2000, 1, 1, 17, 0, 0, 0, time.UTC).Add(time.Second * 0 * time.Duration(rand.Int31n(3600*2)-3600)).Format(TIME_FORMAT_SHIFT)
	}
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

/* this is supposed ot be a rare event, double login/logout etc */
func employeeWorkedThisDaySecondary(employeeWorksThisDay bool) bool {
	c := rand.Int31n(1000)
	if c > 950 {
		return true
	} else {
		return false
	}
}

func employeeWorkedThisDay(employeeWorksThisDay bool) bool {
	c := rand.Int31n(100)
	if employeeWorksThisDay {
		if c > 5 {
			return true
		} else {
			return false
		}
	} else {
		/* logged in when they are not supposed too */
		if c > 95 {
			return true
		} else {
			return false
		}
	}
}

func get_shift_info(Id uint32, Time time.Time) (time.Duration, time.Duration) {
	c := rand.Int31n(100)
	shift_info, _ := db_get_employee_shift_day(Id, Time.Weekday())

	h, m, _ := shift_info.Shift_start.Clock()

	var start_offset time.Duration
	start_offset = time.Duration(h)*time.Hour + time.Duration(m)*time.Minute + time.Duration(rand.Intn(3600*2)-3600)*time.Second

	h, m, _ = shift_info.Shift_end.Clock()

	var end_offset time.Duration
	end_offset = time.Duration(h)*time.Hour + time.Duration(m)*time.Minute + time.Duration(rand.Intn(3600*2)-3600)*time.Second

	if shift_info.Shift_start.IsZero() {
		start_offset = time.Duration(8*3600+(rand.Intn(3600*2)-3600)) * time.Second
		end_offset = time.Duration(17*3600+(rand.Intn(3600*2)-3600)) * time.Second
	}

	/* occasianly flip times */
	if c > 95 {
		return end_offset, start_offset
	}

	return start_offset, end_offset
}

func fake_login_logout(Id uint32, Name string, Time time.Time) {
	/* weekend */
	if Time.Weekday() == 0 || Time.Weekday() == 6 {
		return
	}

	shift, err := db_get_employee_shift(Id)
	if !err {
		panic(0)
	}

	fmt.Println("Employee = ", Name, "Time = ", Time, "weekday==", Time.Weekday())
	start_offset, end_offset := get_shift_info(Id, Time)

	/* these are primary login/logouts */
	if Time.Weekday() == 1 {
		if employeeWorkedThisDay(!shift.Shift_m_s.IsZero()) {
			fmt.Println(Name, "Logged in")
			db_put(0xdeadbeef, PARALLAX_SIGN_IN, Id, Time.Add(start_offset))
		}
		if employeeWorkedThisDay(!shift.Shift_m_s.IsZero()) {
			fmt.Println(Name, "Logged out")
			db_put(0xdeadbeef, PARALLAX_SIGN_OUT, Id, Time.Add(end_offset))
		}
	}

	if Time.Weekday() == 2 {
		if employeeWorkedThisDay(!shift.Shift_t_s.IsZero()) {
			fmt.Println(Name, "Logged in")
			db_put(0xdeadbeef, PARALLAX_SIGN_IN, Id, Time.Add(start_offset))
		}
		if employeeWorkedThisDay(!shift.Shift_t_s.IsZero()) {
			fmt.Println(Name, "Logged out")
			db_put(0xdeadbeef, PARALLAX_SIGN_OUT, Id, Time.Add(end_offset))
		}
	}

	if Time.Weekday() == 3 {
		if employeeWorkedThisDay(!shift.Shift_w_s.IsZero()) {
			fmt.Println(Name, "Logged in")
			db_put(0xdeadbeef, PARALLAX_SIGN_IN, Id, Time.Add(start_offset))
		}
		if employeeWorkedThisDay(!shift.Shift_w_s.IsZero()) {
			fmt.Println(Name, "Logged out")
			db_put(0xdeadbeef, PARALLAX_SIGN_OUT, Id, Time.Add(end_offset))
		}
	}

	if Time.Weekday() == 4 {
		if employeeWorkedThisDay(!shift.Shift_th_s.IsZero()) {
			fmt.Println(Name, "Logged in")
			db_put(0xdeadbeef, PARALLAX_SIGN_IN, Id, Time.Add(start_offset))
		}
		if employeeWorkedThisDay(!shift.Shift_th_s.IsZero()) {
			fmt.Println(Name, "Logged out")
			db_put(0xdeadbeef, PARALLAX_SIGN_OUT, Id, Time.Add(end_offset))
		}
	}

	if Time.Weekday() == 5 {
		if employeeWorkedThisDay(!shift.Shift_f_s.IsZero()) {
			fmt.Println(Name, "Logged in")
			db_put(0xdeadbeef, PARALLAX_SIGN_IN, Id, Time.Add(start_offset))
		}
		if employeeWorkedThisDay(!shift.Shift_f_s.IsZero()) {
			fmt.Println(Name, "Logged out")
			db_put(0xdeadbeef, PARALLAX_SIGN_OUT, Id, Time.Add(end_offset))
		}
	}

	/* these are "seconday accidental login/logouts */
	for i := 0; i < 3; i++ {
		start_offset, end_offset := get_shift_info(Id, Time)
		if Time.Weekday() == 1 {
			if employeeWorkedThisDaySecondary(!shift.Shift_m_s.IsZero()) {
				fmt.Println(Name, "Logged in")
				db_put(0xdeadbeef, PARALLAX_SIGN_IN, Id, Time.Add(start_offset))
			}
			if employeeWorkedThisDaySecondary(!shift.Shift_m_s.IsZero()) {
				fmt.Println(Name, "Logged out")
				db_put(0xdeadbeef, PARALLAX_SIGN_OUT, Id, Time.Add(end_offset))
			}
		}

		if Time.Weekday() == 2 {
			if employeeWorkedThisDaySecondary(!shift.Shift_t_s.IsZero()) {
				fmt.Println(Name, "Logged in")
				db_put(0xdeadbeef, PARALLAX_SIGN_IN, Id, Time.Add(start_offset))
			}
			if employeeWorkedThisDaySecondary(!shift.Shift_t_s.IsZero()) {
				fmt.Println(Name, "Logged out")
				db_put(0xdeadbeef, PARALLAX_SIGN_OUT, Id, Time.Add(end_offset))
			}
		}

		if Time.Weekday() == 3 {
			if employeeWorkedThisDaySecondary(!shift.Shift_w_s.IsZero()) {
				fmt.Println(Name, "Logged in")
				db_put(0xdeadbeef, PARALLAX_SIGN_IN, Id, Time.Add(start_offset))
			}
			if employeeWorkedThisDaySecondary(!shift.Shift_w_s.IsZero()) {
				fmt.Println(Name, "Logged out")
				db_put(0xdeadbeef, PARALLAX_SIGN_OUT, Id, Time.Add(end_offset))
			}
		}

		if Time.Weekday() == 4 {
			if employeeWorkedThisDaySecondary(!shift.Shift_th_s.IsZero()) {
				fmt.Println(Name, "Logged in")
				db_put(0xdeadbeef, PARALLAX_SIGN_IN, Id, Time.Add(start_offset))
			}
			if employeeWorkedThisDaySecondary(!shift.Shift_th_s.IsZero()) {
				fmt.Println(Name, "Logged out")
				db_put(0xdeadbeef, PARALLAX_SIGN_OUT, Id, Time.Add(end_offset))
			}
		}

		if Time.Weekday() == 5 {
			if employeeWorkedThisDaySecondary(!shift.Shift_f_s.IsZero()) {
				fmt.Println(Name, "Logged in")
				db_put(0xdeadbeef, PARALLAX_SIGN_IN, Id, Time)
			}
			if employeeWorkedThisDaySecondary(!shift.Shift_f_s.IsZero()) {
				fmt.Println(Name, "Logged out")
				db_put(0xdeadbeef, PARALLAX_SIGN_OUT, Id, Time.Add(end_offset))
			}
		}
	}
}

func main() {
	rand.Seed(time.Now().Unix())
	db_connect()
	db_truncate_timeinfo()
	db_truncate_employeeifo()

	for i := 0; i < 100; i++ {
		test_add_employee()
	}

	emp_arr := db_get_employees()
	if len(emp_arr) == 0 {
		fmt.Println("no employees, can't do a fake login!")
		return
	}

	for _, employee := range emp_arr {
		for i := 0; i < 35; i++ {
			fake_login_logout(employee.Id, employee.Name, time.Date(2020, 9, 1, 0, 0, 0, 0, time.UTC).AddDate(0, 0, i))
		}
	}

	create_excel_document(time.Date(2020, 9, 1, 0, 0, 0, 0, time.UTC), "test_document.xls")

}
