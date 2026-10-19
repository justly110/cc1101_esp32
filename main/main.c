/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/*  CC1101 Multi-band (315/433/443/868/915 MHz) Remote Control Signal System

    This example code is in the Public Domain (or CC0 licensed, at your option.)

    Unless required by applicable law or agreed to in writing, this
    software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, either express or implied.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "cc1101_driver.h"
#include "web_server.h"

/* AP Configuration */
#define EXAMPLE_ESP_WIFI_AP_SSID            CONFIG_ESP_WIFI_AP_SSID
#define EXAMPLE_ESP_WIFI_AP_PASSWD          CONFIG_ESP_WIFI_AP_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL            CONFIG_ESP_WIFI_AP_CHANNEL
#define EXAMPLE_MAX_STA_CONN                CONFIG_ESP_MAX_STA_CONN_AP

static const char *TAG_AP = "WiFi SoftAP";
static const char *TAG_CC1101 = "CC1101";
static const char *TAG_WEB = "WebServer";

/* AP event handler */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG_AP, "Station "MACSTR" joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG_AP, "Station "MACSTR" left, AID=%d, reason:%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
}

/* Initialize soft AP */
esp_netif_t *wifi_init_softap(void)
{
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_AP_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_AP_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_AP_PASSWD,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    if (strlen(EXAMPLE_ESP_WIFI_AP_PASSWD) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    ESP_LOGI(TAG_AP, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_AP_SSID, EXAMPLE_ESP_WIFI_AP_PASSWD, EXAMPLE_ESP_WIFI_CHANNEL);

    return esp_netif_ap;
}

void app_main(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    /* Mount SPIFFS for signal storage */
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t spiffs_ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (spiffs_ret != ESP_OK) {
        ESP_LOGE(TAG_WEB, "Failed to mount SPIFFS: %d", spiffs_ret);
    } else {
        size_t total = 0, used = 0;
        esp_spiffs_info("storage", &total, &used);
        ESP_LOGI(TAG_WEB, "SPIFFS mounted: total=%u, used=%u", (unsigned)total, (unsigned)used);
    }
    
    /* Register AP event handler only (no STA) */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &wifi_event_handler,
                    NULL,
                    NULL));
    
    /* Initialize WiFi in AP mode only */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    
    /* Initialize AP */
    ESP_LOGI(TAG_AP, "ESP_WIFI_MODE_AP");
    esp_netif_t *esp_netif_ap = wifi_init_softap();
    
    /* Start WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());
    
    /* Start AP DHCP server */
    ESP_LOGI(TAG_AP, "Starting AP DHCP server");
    esp_netif_dhcps_start(esp_netif_ap);
    ESP_LOGI(TAG_AP, "AP DHCP started");
    
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(esp_netif_ap, &ip_info) == ESP_OK) {
        ESP_LOGI(TAG_AP, "AP IP: " IPSTR, IP2STR(&ip_info.ip));
    }
    
    vTaskDelay(500 / portTICK_PERIOD_MS); /* Wait for DHCP to stabilize */
    
    /* Set AP as default netif */
    esp_netif_set_default_netif(esp_netif_ap);
    
    /* Start web server */
    ESP_LOGI(TAG_WEB, "Starting web server for CC1101 control");
    ret = web_server_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WEB, "Failed to start web server: %d", ret);
    } else {
        ESP_LOGI(TAG_WEB, "Web server started successfully");
    }
    
    /* Initialize CC1101 once at boot. Capture is triggered from the web UI. */
    ESP_LOGI(TAG_CC1101, "Initializing CC1101");
    ret = cc1101_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_CC1101, "Failed to initialize CC1101: %d", ret);
    } else {
        cc1101_config_t config = {
            .frequency = CC1101_DEFAULT_FREQUENCY,
            .channel = CC1101_CHANNEL_1,
            .modulation = CC1101_MODULATION_GFSK,
            .sample_rate = CC1101_SIGNAL_SAMPLE_RATE,
            .continuous = false,
        };
        ret = cc1101_write_config(&config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_CC1101, "Failed to configure CC1101: %d", ret);
        } else {
            ESP_LOGI(TAG_CC1101, "Default frequency: %.3f MHz (selectable from web UI)",
                     CC1101_DEFAULT_FREQUENCY / 1000000.0);
        }
    }
    
    ESP_LOGI(TAG_AP, "CC1101 remote control system initialized successfully");
    ESP_LOGI(TAG_AP, "Web interface available at: http://192.168.4.1");
    ESP_LOGI(TAG_AP, "Signal capture and transmission ready");
}
