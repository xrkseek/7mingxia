idf_component_register(
    SRCS "main.c" "servo_wiggle.c" "web_portal.c" "ble_ctrl.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        nvs_flash
        bt
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        esp_driver_gpio
        esp_driver_i2s
        esp_driver_ledc
        dns_server
    EMBED_FILES
        "${CMAKE_CURRENT_LIST_DIR}/../spiffs/cicada.wav"
        "${CMAKE_CURRENT_LIST_DIR}/../spiffs/cricket.wav"
        "${CMAKE_CURRENT_LIST_DIR}/../spiffs/mole_cricket.wav"
        "${CMAKE_CURRENT_LIST_DIR}/../spiffs/katydid_day.wav"
        "${CMAKE_CURRENT_LIST_DIR}/../spiffs/katydid_night.wav"
        "${CMAKE_CURRENT_LIST_DIR}/../spiffs/longhorn.wav"
        "${CMAKE_CURRENT_LIST_DIR}/../spiffs/locust.wav"
        "${CMAKE_CURRENT_LIST_DIR}/../spiffs/egg_chou_xuhang.wav"
        "${CMAKE_CURRENT_LIST_DIR}/../web/index.html"
)
