

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "tic_types.h"

#include "uart_events.h"
#include "decode.h"
#include "process.h"
#include "wifi.h"
#include "mqtt.h"

#ifdef CONFIG_TIC_OLED_DISPLAY
  #include "oled.h"
#endif

#ifdef CONFIG_TIC_SNTP
  #include "clock.h"
#endif

#include "event_loop.h"
#include "ticled.h"

#ifdef CONFIG_TIC_CONSOLE
  #include "tic_console.h"
  #include "status.h"
#endif



#define TZSTRING_CET "CET-1CEST,M3.5.0/2,M10.5.0/3"    // [Europe/Paris]


static const char *TAG = "main_app";


void nvs_initialise(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);

//    esp_log_level_set("cmd_tic.c", ESP_LOG_DEBUG);
//    esp_log_level_set("wifi.c", ESP_LOG_WARN);
 //   esp_log_level_set("process.c",ESP_LOG_INFO);
 //   esp_log_level_set("mqtt.c",ESP_LOG_DEBUG);
 //   esp_log_level_set("wifi", ESP_LOG_WARN);
 //   esp_log_level_set("wifi_init", ESP_LOG_WARN);
 //   esp_log_level_set("uart_events.c", ESP_LOG_DEBUG);
  //  esp_log_level_set("event_loop.c", ESP_LOG_DEBUG);
//    esp_log_level_set("status.cpp", ESP_LOG_DEBUG);
 //   esp_log_level_set("decode.c", ESP_LOG_WARN);
  //  esp_log_level_set("ticled.c", ESP_LOG_DEBUG);
    //esp_log_level_set("dataset.c", ESP_LOG_DEBUG);
//    esp_log_level_set("process.c", ESP_LOG_DEBUG);
    //esp_log_level_set("puissance.c", ESP_LOG_DEBUG);
    //esp_log_level_set("oled.cpp", ESP_LOG_DEBUG);


    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    event_loop_init();

    // Non Volatile Storage utilisé pour
    //   - wifi driver
    //   - phy calibration data
    //   - wifi credentials
    //   - mqtt broker adress
    nvs_initialise();
    wifi_task_start();
#ifdef CONFIG_TIC_CONSOLE
    status_init();
    console_task_start();
#endif
#ifdef CONFIG_TIC_SNTP
    sntp_client_initialise();
    setenv( "TZ", TZSTRING_CET, 1);
    tzset();
#endif
#ifdef CONFIG_TIC_OLED_DISPLAY
    oled_task_start();
#endif
    ticled_task_start();
    uart_task_start();
    tic_decode_task_start();
    process_task_start();
    mqtt_task_start( 0 );   // 0=lance le client mqtt   1=dummy/debug
//    start_bouton_task();
}

