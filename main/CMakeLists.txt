# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

# FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

set(srcs
    "main.c"
    "clock.c"
    "mqtt.c"
    "oled.cpp"
    "status.c"
    "decode.c"
    "process.c"
    "puissance.c"
    "dataset.c"
    "ticled.c"
    "uart_events.c"
    "wifi.c"
    "bouton.c"
    "nvs_utils.c"
    "console/console.c"
    "console/cmd_nvs.c" 
    "console/cmd_tic.c"
    "console/cmd_system.c" 
    )


set(includes 
    "include"
    "console" )


idf_component_register( SRCS ${srcs} 
                        INCLUDE_DIRS ${includes}
                        REQUIRES
                            lwip
                            driver
                            esp_wifi
                            esp_event
                            esp_netif
                            esp-tls
                            spi_flash
                            nvs_flash
                            mqtt
                            console
                         )
