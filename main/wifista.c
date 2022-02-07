//
//  wifista.c
//
//
//  (C) 2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * (Very) basic ESP8266 wifi station mode.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include "nvs_flash.h"

#define WIFI_SSID		CONFIG_ESPTIC_WIFI_SSID
#define WIFI_PASS		CONFIG_ESPTIC_WIFI_PASSWORD

#define WIFI_CONNECTED_BIT	BIT0

static const char *TAG = "wifi station";

static EventGroupHandle_t wifi_eg;


static void event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data)
{
	if (event_base == WIFI_EVENT) {
		switch (event_id) {
			case WIFI_EVENT_STA_DISCONNECTED:
				ESP_LOGI(TAG, "retry WiFi connect");
				// fallthrough
			case WIFI_EVENT_STA_START:
				esp_wifi_connect();
				break;
		}
	}
	else if (wifi_eg && (event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP))
		xEventGroupSetBits(wifi_eg, WIFI_CONNECTED_BIT);
}

void wifista_start(void)
{
	EventBits_t bits;

	wifi_eg = xEventGroupCreate();
	if (!wifi_eg) {
		ESP_LOGE(TAG, "Couldn't create event group");
		abort();
	}

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	ESP_ERROR_CHECK(nvs_flash_init());

	ESP_LOGI(TAG, "Begin initialization");

	tcpip_adapter_init();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = WIFI_SSID,
			.password = WIFI_PASS
		},
	};

	/* Setting a password implies station will connect to all security modes including WEP/WPA.
	 * Force WPA2 here if a password is set. */
	if (*wifi_config.sta.password != '\0')
		wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "Initialization complete");

retry:
	bits = xEventGroupWaitBits(wifi_eg, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
	if (bits != WIFI_CONNECTED_BIT) {
		ESP_LOGE(TAG, "Connection timeout");
		goto retry;	// never give up
	}

	// the example unregisters the WIFI event handler here:
	// don't, because if the connection is lost then the system will not reconnect
	ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
	vEventGroupDelete(wifi_eg);
	wifi_eg = NULL;
}
