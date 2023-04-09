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
    "bouton.c"     )

idf_component_register( SRCS ${srcs} 
                        REQUIRES
                            lwip
                            driver
                            esp_wifi
                            esp_event
                            esp_netif  )
