#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "file_core.h"
#include "parallax.h"

#include "console_core.h"

static const char TAG[30] = "CONSOLE_CORE";

static struct {
    struct arg_int* device_id;
    struct arg_str* device_name;
    struct arg_end* end;
} arg_setdevice_params;

static struct {
    struct arg_str* ip;
    struct arg_int* port;
    struct arg_end* end;
} arg_setip;

static struct {
    struct arg_str* ssid;
    struct arg_str* password;
    struct arg_end* end;
} arg_setwifi;

static struct {
    struct arg_str* accept;
    struct arg_end* end;
} arg_reset;

static struct {
    struct arg_end* end;
} arg_reboot;

bool isValidIpAddress(char* ipAddress) {
    struct sockaddr_in sa;
    int                result = inet_pton(AF_INET, ipAddress, &(sa.sin_addr));
    return result;
}

static int system_reboot(int argc, char** argv) {
    ESP_LOGI(TAG, "system rebooting");
    esp_restart();
}

static int system_reset(int argc, char** argv) {
    char accept_string[MAX_ACCEPT_LEN];

    int nerrors = arg_parse(argc, argv, (void**)&arg_reset);
    if (nerrors != 0) {
        arg_print_errors(stderr, arg_setdevice_params.end, argv[0]);
        return 0;
    }

    snprintf(accept_string, MAX_ACCEPT_LEN, "%s", arg_reset.accept->sval[0]);

    int len = strnlen(accept_string, MAX_ACCEPT_LEN - 1);
    if (len != LEN_OF_YES) {
        ESP_LOGE(TAG, "accept must be formated as such: \"reset --accept=yes\" !");
        return 1;
    }

    if (strncmp(accept_string, "yes", LEN_OF_YES) == 0) {
        ESP_LOGI(TAG, "Will wipe device memory (Id, Name, WiFI credentials are not touched)!");

        commandQ_parallax_t parallax_cmd;
        parallax_cmd.command = PARALLAX_DLT_ALL;
        parallax_thread_gate(&parallax_cmd);

        commandQ_file_t file_cmd;
        file_cmd.command = FILE_DELETE_ALL_USERS;
        file_thread_gate(file_cmd);

        //unbrick device
        uint8_t brick_val = NOT_BRICKED;
        file_core_set(NVS_BRICKED, &brick_val);
    } else {
        ESP_LOGE(TAG, "accept must be formated as such: \"reset --accept=yes\" !");
        return 1;
    }

    ESP_LOGI(TAG, "Wiped thumprints and users!");
    return 0;
}

static int setdevice_cmd(int argc, char** argv) {
    int nerrors = arg_parse(argc, argv, (void**)&arg_setdevice_params);
    if (nerrors != 0) {
        arg_print_errors(stderr, arg_setdevice_params.end, argv[0]);
        return 0;
    }
    int deviceId = arg_setdevice_params.device_id->ival[0];
    if (deviceId < 0) {
        ESP_LOGI(TAG, "Negative!! deviceId == %d", deviceId);
        return 1;
    }

    if (deviceId == 0x7FFFFFFF) {
        ESP_LOGI(TAG, "deviceId truncated to 0x7FFFFFFF, make sure it's less than that!");
        return 1;
    }

    /* internally, we assume deviceId is 64 bit in file core */
    uint64_t DeviceId_64 = deviceId;
    ESP_LOGI(TAG, "Setting deviceId ==  %llu", DeviceId_64);

    int store = file_core_set(NVS_DEVICE_ID, &DeviceId_64);
    if (store != ITEM_GOOD) {
        ESP_LOGE(TAG, "Could not commit deviceId to memory!");
        return 1;
    } else {
        ESP_LOGI(TAG, "Commit deviceId to memory!");
    }

    char device_name[MAX_DEVICE_NAME];
    memset(device_name, 0, MAX_DEVICE_NAME);

    snprintf(device_name, MAX_DEVICE_NAME, "%s", arg_setdevice_params.device_name->sval[0]);

    if (strnlen(device_name, MAX_DEVICE_NAME) == 0 || strnlen(device_name, MAX_DEVICE_NAME) == MAX_DEVICE_NAME) {
        ESP_LOGE(TAG, "Something wrong with size of device name must be between (1-50)");
        return 1;
    }

    store = file_core_set(NVS_DEVICE_NAME, device_name);
    if (store != ITEM_GOOD) {
        ESP_LOGE(TAG, "Could not commit device_name to memory!");
        return 1;
    } else {
        ESP_LOGI(TAG, "Commit device name to memory!");
    }
    return 0;
}

static int setwifi_cmd(int argc, char** argv) {
    char ssid[MAX_SSID_LEN];
    char pw[MAX_SSID_LEN];

    int nerrors = arg_parse(argc, argv, (void**)&arg_setwifi);
    if (nerrors != 0) {
        arg_print_errors(stderr, arg_setwifi.end, argv[0]);
        return 0;
    }

    snprintf(ssid, MAX_SSID_LEN, "%s", arg_setwifi.ssid->sval[0]);
    snprintf(pw, MAX_PW_LEN, "%s", arg_setwifi.password->sval[0]);

    if (strnlen(ssid, MAX_SSID_LEN) == 0 || strnlen(ssid, MAX_SSID_LEN) == MAX_SSID_LEN) {
        ESP_LOGE(TAG, "Could not set SSID, please enter in correct format");
        return 1;
    }

    if (strnlen(pw, MAX_PW_LEN) == 0 || strnlen(pw, MAX_PW_LEN) == MAX_PW_LEN) {
        ESP_LOGE(TAG, "Could not set PW, please enter in correct format");
        return 1;
    }

    if (pw[0] == '"' || ssid[0] == '"') {
        ESP_LOGW(TAG, "Did you add quotes? they are not needed...");
    }

    ESP_LOGI(TAG, "SSID = %s, PW = %s", ssid, pw);

    int store = file_core_set(NVS_SSID_NAME, ssid);
    if (store != ITEM_GOOD) {
        ESP_LOGE(TAG, "Could not commit ssid to memory!");
        return 1;
    } else {
        ESP_LOGI(TAG, "Commit ssid to memory!");
    }

    store = file_core_set(NVS_SSID_PW, pw);
    if (store != ITEM_GOOD) {
        ESP_LOGE(TAG, "Could not commit pw to memory!");
        return 1;
    } else {
        ESP_LOGI(TAG, "Commit pw to memory!");
    }
    return 0;
}

static int setip_cmd(int argc, char** argv) {
    ESP_LOGI(TAG, "HERE");
    char ip[MAX_IP_LEN];

    int nerrors = arg_parse(argc, argv, (void**)&arg_setip);
    if (nerrors != 0) {
        arg_print_errors(stderr, arg_setip.end, argv[0]);
        return 0;
    }

    snprintf(ip, MAX_IP_LEN, "%s", arg_setip.ip->sval[0]);
    if (strnlen(ip, MAX_IP_LEN) == 0 || strnlen(ip, MAX_IP_LEN) == MAX_IP_LEN) {
        ESP_LOGE(TAG, "Could not set IP, please enter in correct format");
        return 1;
    }

    bool valid_ip = isValidIpAddress(ip);
    if (!valid_ip) {
        ESP_LOGE(TAG, "IP incorrect! did you include quotes? they are not needed!");
        return 1;
    }

    int port_i = arg_setip.port->ival[0];
    if (0 > port_i || port_i > 65535) {
        ESP_LOGE(TAG, "Port incorrect! range (0-65535)");
        return 1;
    }
    uint16_t port = (uint16_t)(port_i);

    ESP_LOGI(TAG, "Using %s as ip, %hu as port", ip, port);

    int store = file_core_set(NVS_IP, ip);
    if (store != ITEM_GOOD) {
        ESP_LOGE(TAG, "Could not commit ip to memory!");
        return 1;
    } else {
        ESP_LOGI(TAG, "Commit ip to memory!");
    }

    store = file_core_set(NVS_PORT, &port);
    if (store != ITEM_GOOD) {
        ESP_LOGE(TAG, "Could not commit port to memory!");
        return 1;
    } else {
        ESP_LOGI(TAG, "Commit port to memory!");
    }
    return 0;
}

void register_deviceidset() {
    arg_setdevice_params.device_id   = arg_int1(NULL, "id", "[0-2^31 - 1)", "set the deviceId, note, this POS console only accets int(32), hence.. the restriction");
    arg_setdevice_params.device_name = arg_str1(NULL, "device_name", "max 50 len", "Set the device Name (it's shown on the site as this)");
    arg_setdevice_params.end         = arg_end(2);

    const esp_console_cmd_t i2cconfig_cmd = {
        .command  = "set_device_params",
        .help     = "Set device ID and device Name, note that to enter spaces in the device name each space must be espaced with a backlash ie, \"hi\\ sam\"",
        .hint     = NULL,
        .func     = &setdevice_cmd,
        .argtable = &arg_setdevice_params
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cconfig_cmd));
}

void register_ipset() {
    arg_setip.ip   = arg_str1(NULL, "ip", "ipv4 address", "set the ip");
    arg_setip.port = arg_int1(NULL, "port", "port address", "set the port");
    arg_setip.end  = arg_end(2);

    const esp_console_cmd_t i2cconfig_cmd = {
        .command  = "set_ip",
        .help     = "Set the device IP:PORT",
        .hint     = NULL,
        .func     = &setip_cmd,
        .argtable = &arg_setip
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cconfig_cmd));
}

void register_wifi() {
    arg_setwifi.ssid     = arg_str1(NULL, "ssid", "wifi ssid", "set the SSID ");
    arg_setwifi.password = arg_str1(NULL, "pw", "wifi password", "set the SSID password");
    arg_setwifi.end      = arg_end(2);

    const esp_console_cmd_t i2cconfig_cmd = {
        .command  = "set_wifi",
        .help     = "Set the wifi credentials",
        .hint     = NULL,
        .func     = &setwifi_cmd,
        .argtable = &arg_setwifi
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cconfig_cmd));
}

void register_reset() {
    arg_reset.accept = arg_str1(NULL, "agree", "\"yes\"", "Erase all users");
    arg_reset.end    = arg_end(2);

    const esp_console_cmd_t i2cconfig_cmd = {
        .command  = "reset",
        .help     = "reset --yes, will prompt a system reset and wipe all users",
        .hint     = NULL,
        .func     = &system_reset,
        .argtable = &arg_reset
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cconfig_cmd));
}

void register_reboot() {
    arg_reboot.end = arg_end(2);

    const esp_console_cmd_t i2cconfig_cmd = {
        .command  = "reboot",
        .help     = "reboot device",
        .hint     = NULL,
        .func     = &system_reboot,
        .argtable = &arg_reboot
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cconfig_cmd));
}

void register_console(void) {
    register_deviceidset();
    register_ipset();
    register_wifi();
    register_reboot();
    register_reset();
}

void console_init() {
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt                    = "timeScan >> ";
    ESP_ERROR_CHECK(esp_console_repl_init(&repl_config));

    register_console();

    printf("****************************************************************************************\n");
    printf("*                                                                                      *\n");
    printf("*  TIMESCAN CONSOLE SERVICE                                                            *\n");
    printf("*   NOTE: escape \"space\" with a backlash                                               *\n");
    printf("*   type \"help\" for help                                                               *\n");
    printf("*                                                                                      *\n");
    printf("* example setup:                                                                       *\n");
    printf("*   set_device_params --id=1234 --device_name=United\\ Atheist\\ Front\\ Of\\ Kirachi      *\n");
    printf("*   set_ip --ip=192.168.0.189 --port=3334                                              *\n");
    printf("*   set_wifi --ssid=LinearAmp --pw=password                                            *\n");
    printf("*                                                                                      *\n");
    printf("* System reboot:                                                                       *\n");
    printf("*   reboot                                                                             *\n");
    printf("*                                                                                      *\n");
    printf("* Erase All Users:                                                                     *\n");
    printf("*   reset --agree=yes                                                                  *\n");
    printf("*                                                                                      *\n");
    printf("****************************************************************************************\n");

    ESP_ERROR_CHECK(esp_console_repl_start());
}
