idf_component_register(SRCS "softap_sta.c" "cc1101_driver.c" "web_server.c"
                    PRIV_REQUIRES esp_wifi nvs_flash esp_driver_spi esp_driver_gpio esp_http_server esp_timer json
                    INCLUDE_DIRS ".")
