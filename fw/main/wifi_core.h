#pragma once

#include "esp_err.h"
#include "esp_netif.h"
#include "stdint.h"

// sam hack to get the wifi working
#define CONFIG_EXAMPLE_CONNECT_WIFI
#define CONFIG_EXAMPLE_CONNECT_IPV6
#define CONFIG_EXAMPLE_CONNECT_IPV6_PREF_LOCAL_LINK
#define CONFIG_EXAMPLE_WIFI_SSID "foo"

#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
#define EXAMPLE_INTERFACE get_example_netif()
#endif

#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
#define EXAMPLE_INTERFACE get_example_netif()
#endif

#define WIFI_STATE_CONNECTED    (0)
#define WIFI_STATE_DISCONNECTED (1)

#define WIFI_MUTEX_WAIT (5000 / portTICK_PERIOD_MS)
#define WIFI_DOWN       (0)
#define WIFI_UP         (1)

// does all the heavy lifting to connect to the wifi
int wifi_connect();

/**
 * @brief Configure Wi-Fi or Ethernet, connect, wait for IP
 *
 * This all-in-one helper function is used in protocols examples to
 * reduce the amount of boilerplate in the example.
 *
 * It is not intended to be used in real world applications.
 * See examples under examples/wifi/getting_started/ and examples/ethernet/
 * for more complete Wi-Fi or Ethernet initialization code.
 *
 * Read "Establishing Wi-Fi or Ethernet Connection" section in
 * examples/protocols/README.md for more information about this function.
 *
 * @return ESP_OK on successful connection
 */
esp_err_t example_connect(void);

/**
 * Counterpart to example_connect, de-initializes Wi-Fi or Ethernet
 */
esp_err_t example_disconnect(void);

/**
 * @brief Configure stdin and stdout to use blocking I/O
 *
 * This helper function is used in ASIO examples. It wraps installing the
 * UART driver and configuring VFS layer to use UART driver for console I/O.
 */
esp_err_t example_configure_stdin_stdout(void);

/**
 * @brief Returns esp-netif pointer created by example_connect()
 *
 */
esp_netif_t* get_example_netif(void);

void     wifi_core_init_freertos_objects();
void     set_wifi_state(uint32_t state);
uint32_t get_wifi_state();
