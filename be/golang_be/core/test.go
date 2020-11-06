package main

import "github.com/tealeg/xlsx"
import "fmt"

func main() {
	var file *xlsx.File
	var sheet *xlsx.Sheet
	var row *xlsx.Row
	var cell *xlsx.Cell
	var err error

	file = xlsx.NewFile()
	sheet, err = file.AddSheet("Sheet1")
	row = sheet.AddRow()
	cell = row.AddCell()
	cell.SetInt(66)

	style1 := xlsx.NewStyle()
	style1.Font = *xlsx.NewFont(10, "Verdana")
	style1.Fill = *xlsx.NewFill("solid", "FFFFFF", "00000000")
	style1.Border = *xlsx.NewBorder("medium", "medium", "medium", "medium")
	style1.ApplyFill = true
	style1.ApplyFont = true
	style1.ApplyBorder = true

	cell.SetStyle(style1)

	cell = row.AddCell()
	cell.SetStyle(style1)

	err = file.Save("./demo_write.xlsx")
	if err != nil {
		fmt.Printf(err.Error())
	}
}
