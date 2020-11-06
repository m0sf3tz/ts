package main

import "os/exec"
import "time"
import "fmt"
import "bytes"

func create_r_plots(deviceId uint64) {
	current_time_local := time.Now().Local()
	current_date := current_time_local.Format("2006-01-02")

	cmd := exec.Command("Rscript", "--vanilla", "plotter.r", current_date, "\"name\"", "4", "09:00:00", "17:00:00", "checkin_co", "output")
	cmd.Dir = "./rscripts/HR-plot/"

	var outb, errb bytes.Buffer
	cmd.Stdout = &outb
	cmd.Stderr = &errb
	err := cmd.Run()
	fmt.Println("out:", outb.String(), "err:", errb.String())

	if err != nil {
		fmt.Println("error, faile in go script", err)
	}

	cmd = exec.Command("cp", "rscripts/HR-plot/output_1.png", "static")
	err = cmd.Run()
	if err != nil {
		fmt.Println("error, could not move plot into static!", err)
	}
}
