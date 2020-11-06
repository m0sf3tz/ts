package main

import "github.com/tealeg/xlsx"
import "fmt"
import "time"
import "sort"
import "strconv"

const RED_STYLE = 1
const BOLD_STYLE = 2

var idToPayPeriodDeetz map[uint32]db_hours_pay_period

func getStyles(s int) *xlsx.Style {
	if s == RED_STYLE {
		style1 := xlsx.NewStyle()
		style1.Fill = *xlsx.NewFill("solid", "FF0000", "00000000")
		return style1
	}

	if s == BOLD_STYLE {
		style2 := xlsx.NewStyle()
		style2.Font.Bold = true
		style2.Font.Size = 13
	}

	return xlsx.NewStyle()
}

func truncate_float(f float64) string {
	return fmt.Sprintf("%.2f", f)
}

func create_exel_main_row(sheet *xlsx.Sheet, pay_period db_hours_pay_period, name string) {
	var style *xlsx.Style
	if pay_period.actual_hours.Hours() < pay_period.expected_hours.Hours() {
		style = getStyles(RED_STYLE)
	}

	row := sheet.AddRow()

	row.AddCell()

	cell := row.AddCell()
	cell.SetString(name)
	cell.SetStyle(style)

	cell = row.AddCell()
	cell.SetString(truncate_float(pay_period.actual_hours.Hours()))
	cell.SetStyle(style)

	cell = row.AddCell()
	cell.SetString(truncate_float(pay_period.expected_hours.Hours()))
	cell.SetStyle(style)

	cell = row.AddCell()
	if pay_period.anomolies {
		cell.SetString(" YES ")
	}
	cell.SetStyle(style)
}

func setup_header_first_page(sheet *xlsx.Sheet) {
	sheet.SetColWidth(1, 4, 20)

	sheet.AddRow()
	sheet.AddRow()

	row := sheet.AddRow()
	cell := row.AddCell()

	cell = row.AddCell()
	cell.SetString("Name")
	cell.SetStyle(getStyles(BOLD_STYLE))

	cell = row.AddCell()
	cell.SetString("Total Worked")
	cell.SetStyle(getStyles(BOLD_STYLE))

	cell = row.AddCell()
	cell.SetString("Expected Worked")
	cell.SetStyle(getStyles(BOLD_STYLE))

	cell = row.AddCell()
	cell.SetString("Flagged")
	cell.SetStyle(getStyles(BOLD_STYLE))
}

func setup_header_sub_page(sheet *xlsx.Sheet, name string, id uint32) {
	sheet.SetColWidth(1, 9, 18)
	sheet.SetColWidth(10, 10, 60)

	row := sheet.AddRow()
	cell := row.AddCell()

	row = sheet.AddRow()
	row.AddCell()
	cell = row.AddCell()
	cell.SetStyle(getStyles(BOLD_STYLE))
	cell.SetString("Name: ")
	cell = row.AddCell()
	cell.SetStyle(getStyles(BOLD_STYLE))
	cell.SetString(name)

	row = sheet.AddRow()
	row.AddCell()
	cell = row.AddCell()
	cell.SetString("Expected Hours:")
	cell.SetStyle(getStyles(BOLD_STYLE))
	cell = row.AddCell()
	cell.SetString(idToPayPeriodDeetz[id].expected_hours.String())
	cell.SetStyle(getStyles(BOLD_STYLE))

	row = sheet.AddRow()
	row.AddCell()
	cell = row.AddCell()
	cell.SetString("Actual Hours:")
	cell.SetStyle(getStyles(BOLD_STYLE))
	cell = row.AddCell()
	cell.SetString(idToPayPeriodDeetz[id].actual_hours.String())
	cell.SetStyle(getStyles(BOLD_STYLE))

	row = sheet.AddRow()
	row = sheet.AddRow()
	row.AddCell()
	cell = row.AddCell()
	cell.SetString("Expected Salary:")
	cell.SetStyle(getStyles(BOLD_STYLE))
	cell = row.AddCell()
	cell.SetString(fmt.Sprintf("$%.2f", idToPayPeriodDeetz[id].exptected_sal))
	cell.SetStyle(getStyles(BOLD_STYLE))

	row = sheet.AddRow()
	row.AddCell()
	cell = row.AddCell()
	cell.SetString("Actual Salary:")
	cell.SetStyle(getStyles(BOLD_STYLE))
	cell = row.AddCell()
	cell.SetString(fmt.Sprintf("$%.2f", idToPayPeriodDeetz[id].actual_sal))
	cell.SetStyle(getStyles(BOLD_STYLE))

	sheet.AddRow()
	sheet.AddRow()

	row = sheet.AddRow()
	row.AddCell()
	cell = row.AddCell()
	cell.SetString("Date")
	cell.SetStyle(getStyles(BOLD_STYLE))

	cell = row.AddCell()
	cell.SetString("Weekday")
	cell.SetStyle(getStyles(BOLD_STYLE))

	cell = row.AddCell()
	cell.SetString("Expected In")
	cell.SetStyle(getStyles(BOLD_STYLE))

	cell = row.AddCell()
	cell.SetString("Expected Out")
	cell.SetStyle(getStyles(BOLD_STYLE))

	cell = row.AddCell()
	cell.SetString("Actual In")
	cell.SetStyle(getStyles(BOLD_STYLE))

	cell = row.AddCell()
	cell.SetString("Actual Out")
	cell.SetStyle(getStyles(BOLD_STYLE))

	cell = row.AddCell()
	cell.SetString("Expected Hours")
	cell.SetStyle(getStyles(BOLD_STYLE))

	cell = row.AddCell()
	cell.SetString("Actual Hours")
	cell.SetStyle(getStyles(BOLD_STYLE))

	cell = row.AddCell()
	cell.SetString("Shift Difference")
	cell.SetStyle(getStyles(BOLD_STYLE))

	cell = row.AddCell()
	cell.SetString("Flag")
	cell.SetStyle(getStyles(BOLD_STYLE))
}

func create_row_sub_page(comp []db_composite_per_day, sheet *xlsx.Sheet, name string, id uint32) {
	setup_header_sub_page(sheet, name, id)
	for _, day := range comp {
		row := sheet.AddRow()
		cell := row.AddCell()
		cell = row.AddCell()
		cell.SetString(day.date.Format(DATE_FORMAT))

		cell = row.AddCell()
		cell.SetString(day.date.Weekday().String())

		/* shift start, expted */
		cell = row.AddCell()
		if day.expected_shift.Shift_start.IsZero() {
			cell.SetString("N/A")
		} else {
			cell.SetString(day.expected_shift.Shift_start.Format(TIME_FORMAT))
		}

		/* shift end, expted */
		cell = row.AddCell()
		if day.expected_shift.Shift_end.IsZero() {
			cell.SetString("N/A")
		} else {
			cell.SetString(day.expected_shift.Shift_end.Format(TIME_FORMAT))
		}

		/* shift start, actuall */
		cell = row.AddCell()
		if day.actual_shift.Shift_start.IsZero() {
			cell.SetString("N/A")
		} else {
			cell.SetString(day.actual_shift.Shift_start.Format(TIME_FORMAT))
		}

		/* shift end, actuall */
		cell = row.AddCell()
		if day.actual_shift.Shift_end.IsZero() {
			cell.SetString("N/A")
		} else {
			cell.SetString(day.actual_shift.Shift_end.Format(TIME_FORMAT))
		}

		/* expted hours worked*/
		cell = row.AddCell()
		if day.expected_hours == 0 {
			cell.SetString("N/A")
		} else {
			cell.SetString(day.expected_hours.String())
		}

		cell = row.AddCell()
		if day.expected_hours == 0 {
			cell.SetString("N/A")
		} else {
			cell.SetString(day.actual_hours.String())
		}

		/*delta*/
		cell = row.AddCell()
		if day.delta == 0 {
			cell.SetString("N/A")
		} else {
			cell.SetString(day.delta.String())
			if day.delta < 0 {
				cell.SetStyle(getStyles(RED_STYLE))
			}
		}

		/* flag*/
		cell = row.AddCell()
		if day.anomolies == 0 {
			cell.SetString("")
		} else {
			cell.SetString(db_get_anomoly_string(day.anomolies))
			cell.SetStyle(getStyles(RED_STYLE))
		}

	}
}

func create_sub_page(Time time.Time, file *xlsx.File) {
	emp_arr := db_get_employees()
	/* sort by name, alphabitaclly */
	sort.Slice(emp_arr, func(i, j int) bool { return emp_arr[i].Name < emp_arr[j].Name })

	logger(PRINT_NORMAL, emp_arr)

	for _, emp := range emp_arr {

		//max name for an excel page is 31
		sheetName := emp.Name
		if len(emp.Name) > 31 {
			sheetName = emp.Name[:31]
		}

		sheet, err := file.AddSheet(sheetName)
		if err != nil {
			panic(err)
		}

		arr := db_get_pay_period_arr(emp.Id, Time)
		create_row_sub_page(arr, sheet, emp.Name, emp.Id)
	}
}

func create_first_page(Time time.Time, sheet *xlsx.Sheet) {
	emp_arr := db_get_employees()
	/* sort by name, alphabitaclly */
	sort.Slice(emp_arr, func(i, j int) bool { return emp_arr[i].Name < emp_arr[j].Name })

	for i, emp := range emp_arr {
		if i%45 == 0 {
			/* don't know how to make these things float */
			setup_header_first_page(sheet)
		}
		pay_period := db_get_total_hours_in_period(emp.Id, Time)
		idToPayPeriodDeetz[emp.Id] = pay_period
		create_exel_main_row(sheet, pay_period, emp.Name)
	}
}

func create_excel_document(Time time.Time, file_name string) {
	if Time.Day() != 1 && Time.Day() != 16 {
		logger(PRINT_FATAL, "Can only accept on 16 and 1st day of month")
	}

	if file_name == "" {
		logger(PRINT_FATAL, "Name Null!")
	}

	var file *xlsx.File
	var sheet *xlsx.Sheet
	var err error
	idToPayPeriodDeetz = make(map[uint32]db_hours_pay_period)

	file = xlsx.NewFile()
	sheet, err = file.AddSheet("overview")
	create_first_page(Time, sheet)

	create_sub_page(Time, file)

	err = file.Save("./static/" + file_name)
	if err != nil {
		logger(PRINT_NORMAL, err.Error())
	}
}

func xl_get_filename(year, month, period int) string {
	return strconv.Itoa(year) + "_" + strconv.Itoa(month) + "_" + strconv.Itoa(period) + ".xlsx"
}

func xl_archive() {
	for {
		/* 13 hours is fucking, but you are gurenteed to run once a day*/
		time.Sleep(time.Hour * 13)

		date := time.Now()

		year, month, day := date.Date()
		if day == 16 || day == 1 {
			archiveName := strconv.Itoa(year) + "_" + strconv.Itoa(int(month)) + "_" + strconv.Itoa(day) + ".xlsx"
			create_excel_document(date, archiveName)
		}
	}
}

func xl_create_this_period() {
	logger(PRINT_NORMAL, "Creating spreadsheet for this period!")

	date := time.Now()
	year, month, day := date.Date()

	if day > 16 {
		day = 16
		/* period for 16 - end of motnh */
		Time := time.Date(year, month, day, 0, 0, 0, 0, time.UTC)
		create_excel_document(Time, xl_get_filename(year, int(month), day))
	} else {
		day = 1
		Time := time.Date(year, month, day, 0, 0, 0, 0, time.UTC)
		create_excel_document(Time, xl_get_filename(year, int(month), day))
	}
}
