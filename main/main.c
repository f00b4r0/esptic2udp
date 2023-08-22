//
//  main.c
//  app for ESP8266/ESP32
//
//  (C) 2021-2022 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Receives TIC on RX, outputs JSON via UDP.
 * @note: The following memory usage was observed while running tic2json_main() as part of app_main(),
 * tested on ESP8266 in "Release" (-Os) build:
 *  - TICV01: max stack 5400, max heap: 3764+80
 *  - TICV02: max stack 5816, max heap: 3764+80
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_vfs_dev.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

#include "tic2json.h"

#include "simple_network.h"

#define XSTR(s) STR(s)
#define STR(s) #s

#define LED_GPIO	CONFIG_ESPTIC2UDP_LED_GPIO_NUM
#define ENABLE_GPIO	CONFIG_ESPTIC2UDP_ENABLE_GPIO_NUM
#define UDPBUFSIZE	2048	// presumably large enough for most TICs fully decoded

#ifdef CONFIG_IDF_TARGET_ESP8266
 #define LED_GPIO_DIR	GPIO_MODE_OUTPUT
#else	/* ESP32 variants */
 #define LED_GPIO_DIR	GPIO_MODE_INPUT_OUTPUT
#endif
#define ENABLE_GPIO_DIR	GPIO_MODE_OUTPUT

static const char * TAG = "esptic2udp";
static struct sockaddr_storage Gai_addr;
static socklen_t Gai_addrlen;
static int Gsockfd;

void tic2json_main(FILE * yyin, int optflags, char * buf, size_t size, tic2json_framecb_t cb);

static int udp_setup(void)
{
	struct addrinfo hints, *result, *rp;
	int ret;

	// obtain address(es) matching host/port
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	ret = getaddrinfo(CONFIG_ESPTIC2UDP_UDP_HOST, CONFIG_ESPTIC2UDP_UDP_PORT, &hints, &result);
	if (ret) {
		ESP_LOGE(TAG, "getaddrinfo: %d", ret);
		return ESP_FAIL;
	}

	// try each address until one succeeds
	for (rp = result; rp; rp = rp->ai_next) {
		Gsockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (-1 != Gsockfd)
			break;	// success
	}

	if (!rp) {
		ESP_LOGE(TAG, "Could not reach server");
		ret = ESP_FAIL;
		goto cleanup;
	}

	memcpy(&Gai_addr, rp->ai_addr, rp->ai_addrlen);
	Gai_addrlen = rp->ai_addrlen;

	ret = ESP_OK;

cleanup:
	freeaddrinfo(result);
	return (ret);

}

static void ticframecb(char * buf, size_t size, bool valid)
{
	if (valid)
		sendto(Gsockfd, buf, size, 0, (struct sockaddr *)&Gai_addr, Gai_addrlen);

#ifdef CONFIG_ESPTIC2UDP_HAS_LED
	// blink after each complete frame
	gpio_set_level(LED_GPIO, !gpio_get_level(LED_GPIO));
#endif
}

static void tic_task(void *pvParameter)
{
	FILE *yyin;
	static char buf[UDPBUFSIZE];

	yyin = fopen("/dev/uart/" XSTR(CONFIG_ESPTIC2UDP_UART_NUM), "r");
	if (!yyin) {
		ESP_LOGE(TAG, "Cannot open UART");
		abort();
	}

	while (1)
		tic2json_main(yyin, 0
#ifdef CONFIG_TIC2JSON_MASKZEROES
			      |TIC2JSON_OPT_MASKZEROES
#endif
#ifdef CONFIG_TIC2JSON_DICTOUT
			      |TIC2JSON_OPT_DICTOUT
#endif
#ifdef CONFIG_TIC2JSON_LONGDATE
			      |TIC2JSON_OPT_LONGDATE
#endif
			      , buf, UDPBUFSIZE, ticframecb);
}

void app_main(void)
{
	BaseType_t ret;

	uart_config_t uart_config = {
		.baud_rate = CONFIG_ESPTIC2UDP_BAUDRATE,
		.data_bits = UART_DATA_7_BITS,
		.parity    = UART_PARITY_EVEN,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#ifndef CONFIG_IDF_TARGET_ESP8266
		.source_clk = UART_SCLK_XTAL,	// for DFS compatibility on ESP32
#endif
	};
	ESP_ERROR_CHECK(uart_param_config(CONFIG_ESPTIC2UDP_UART_NUM, &uart_config));

#ifndef CONFIG_IDF_TARGET_ESP8266
	ESP_ERROR_CHECK(uart_set_pin(CONFIG_ESPTIC2UDP_UART_NUM,
				     UART_PIN_NO_CHANGE, CONFIG_ESPTIC2UDP_UART_RXD,
				     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
#endif

	/* Install UART driver for interrupt-driven reads and writes */
	ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESPTIC2UDP_UART_NUM,
		UART_FIFO_LEN*2, 0, 0, NULL, 0));

	/* Tell VFS to use UART driver */
	esp_vfs_dev_uart_use_driver(CONFIG_ESPTIC2UDP_UART_NUM);

#ifndef CONFIG_IDF_TARGET_ESP8266
	// on ESP32: disable serial line endings translation which breaks everything
	esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESPTIC2UDP_UART_NUM, ESP_LINE_ENDINGS_LF);
	esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESPTIC2UDP_UART_NUM, ESP_LINE_ENDINGS_LF);
#endif

	ESP_ERROR_CHECK(esp_event_loop_create_default());

	simple_network_start();

	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));

	/* setup UDP client */
	ESP_ERROR_CHECK(udp_setup());

#ifdef CONFIG_ESPTIC2UDP_HAS_LED
	ESP_ERROR_CHECK(gpio_set_direction(LED_GPIO, LED_GPIO_DIR));
	gpio_set_level(LED_GPIO, CONFIG_ESPTIC2UDP_LED_ACTIVE_STATE);
#endif

	// wait 10s before starting TIC processing - can't use Light Sleep with network active
	vTaskDelay(pdMS_TO_TICKS(10*1000));

#ifdef CONFIG_ESPTIC2UDP_HAS_ENABLE
	// enable TIC2UART
	ESP_ERROR_CHECK(gpio_set_direction(ENABLE_GPIO, ENABLE_GPIO_DIR));
	gpio_set_level(ENABLE_GPIO, CONFIG_ESPTIC2UDP_ENABLE_ACTIVE_STATE);
#endif

	ret = xTaskCreate(&tic_task, "tic", 8192, NULL, 5, NULL);
	if (ret != pdPASS) {
		ESP_LOGE(TAG, "Failed to create tic task");
		abort();
	}

	ESP_LOGI(TAG, "Rock'n'roll");
}
