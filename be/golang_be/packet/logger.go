package main

import "fmt"
import "time"
import "strings"
import "os"
import "bytes"

var logHandle *os.File

func check(e error) {
	if e != nil {
		panic(e)
	}
}

func logger_init(source string) {
	timeString := time.Now().Format("02-Jan-2006 15:04:05")
	timeString = strings.Replace(timeString, " ", "-", -1)

	var err error
	logHandle, err = os.Create("./log/" + timeString + source)
	check(err)
}

func ms_since_epoch() int64 {
	return time.Now().UnixNano() / 1e6
}

func logger(level int, a ...interface{}) {
	var buf bytes.Buffer

	if level >= CURRENT_LOG_LEVEL {
		fmt.Println(time.Now().Format("15:04:05.00000"), a)

		fmt.Fprint(&buf, a)
		logHandle.Write(buf.Bytes())
		logHandle.Write([]byte("\n"))
	}

	if level == PRINT_FATAL {
		logHandle.Write([]byte("**** FATAL ****"))
		panic(0) // time to die :(
	}
}

func logger_id(level int, id uint64, a ...interface{}) {
	var buf bytes.Buffer

	if level >= CURRENT_LOG_LEVEL {
		fmt.Println(time.Now().Format("15:04:05.00000"), "DeviceID", id, ":", a)

		fmt.Fprint(&buf, a)
		logHandle.Write(buf.Bytes())
		logHandle.Write([]byte("\n"))
	}
	if level == PRINT_FATAL {
		logHandle.Write([]byte("**** FATAL ****"))
		panic(0) // time to die :(
	}
}
