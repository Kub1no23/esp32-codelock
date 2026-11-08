idf_component_register(SRCS "touch.c" "keypad.c" "wifi.c" "main.c" "mqtt.c"
                            INCLUDE_DIRS "."
                            REQUIRES ili-display nvs_flash esp_wifi esp_event mqtt json esp_timer)