package main

// Translates JSON commands from the site
// to UINT8 that the rest of the core can understand
func json_cmd_translate(cmd string) uint8 {
	switch cmd {
	case "add_user":
		return ADD_USER_CMD
	case "delete_all_users":
		return DELETE_ALL_USERS_CMD
	case "get_all_users":
		return GET_ALL_USERS_CMD
	case "delete_specific_user":
		return DELETE_SPECIFIC_USER_CMD
	case "update_user":
		return UPDATE_USER_CMD
	case "push_user":
		return PUSH_USER_CMD
	case "generate_report":
		return GENERATE_REPORT
	case "get_all_devices":
		return GET_ALL_DEVICES
	case "get_all_users_per_device":
		return GET_ALL_USERS_PER_DEVICE
	case "force_sync":
		return FORCE_SYNC
	case "generate_device_id":
		return GENERATE_DEVICE_ID
	case "hard_reset":
		return HARD_RESET
	default:
		logger(PRINT_FATAL, "cmd=", cmd)
	}
	return 0xFF
}
