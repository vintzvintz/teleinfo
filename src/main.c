

/* Intellisense bullshit */
//#undef __linux__

//#define LWIP_DEBUG 1


#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "uart_events.h"
#include "decode.h"
#include "process.h"
#include "wifi.h"
#include "mqtt.h"
#include "oled.h"
#include "ticled.h"
#include "clock.h"
#include "status.h"
#include "bouton.h"


#define TZSTRING_CET         "CET-1CEST,M3.5.0/2,M10.5.0/3"    // [Europe/Paris]


static const char *TAG = "main_app";


void nvs_initialise(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}


void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_DEBUG);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("ticled.c", ESP_LOG_INFO);


    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    status_init();
    nvs_initialise();    // required for wifi driver
    //wifi_task_start();


    setenv( "TZ", TZSTRING_CET, 1);
    tzset();

//    start_bouton_task();
    oled_task_start();
    ticled_task_start();
    uart_task_start();
    tic_decode_task_start();
    process_task_start();
    mqtt_dummy_task_start();
//    clock_task_start();
}

