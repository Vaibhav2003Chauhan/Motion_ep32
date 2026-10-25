idf_component_register(
    SRCS "main.c"
         "lis3dh/lis3dh.c"
         "lis3dh/orientation.c"
         "alert/crash_detect.c"
         "alert/alert_sender.c"
         "wifi/wifi_manager.c"
         "wifi/wifi_scanner.c"
         "wifi/wifi_connect.c"
         "util/serial_input.c"
         "ble/ble_config_service.c"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash
)
