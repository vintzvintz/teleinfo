



#define LWIP_DEBUG 1


#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/stream_buffer.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"

#include "tic_decode.h"
#include "uart_events.h"
#include "wifi.h"
#include "mqtt.h"
#include "oled.h"
#include "ticled.h"
#include "clock.h"
#include "status.h"




static const char *TAG = "main_app";

#define UART_STREAMBUFFER_SIZE 512
#define UART_STREAMBUFFER_TRIGGER 16



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
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);


    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    // transfere le flux de données brutes depuis l'UART vers le decodeur
    StreamBufferHandle_t to_decoder = xStreamBufferCreate( UART_STREAMBUFFER_SIZE, UART_STREAMBUFFER_TRIGGER );
    //ESP_LOGI(TAG, "to_decoder=%p", to_decoder );
    if( to_decoder == NULL )
    {
        ESP_LOGE( TAG, "Failed to create to_decoder StreamBuffer" );
    }

    // transfere les trames depuis le decodeur vers le client MQTT pour publication
     QueueHandle_t to_mqtt = xQueueCreate( 5, sizeof( tic_dataset_t * ) );
    //ESP_LOGI(TAG, "to_mqtt=%p", to_mqtt );
    if( to_mqtt == NULL )
    {
        ESP_LOGE( TAG, "Failed to create to_mqtt Queue" );
    }

    // controle la led ptiInfo
    EventGroupHandle_t to_ticled = xEventGroupCreate();
    //ESP_LOGI(TAG, "to_blink=%p", to_ticled );
    if( to_ticled == NULL )
    {
        ESP_LOGE( TAG, "Failed to create to_blink EventGroup" );
    }

    // Reception des infos à afficher sur l'écran OLED
    QueueHandle_t to_oled = xQueueCreate( 50, sizeof( display_event_t ) );
    //ESP_LOGI(TAG, "to_oled=%p", to_oled );
    if( to_oled == NULL )
    {
        ESP_LOGE( TAG, "Failed to create to_oled Queue" );
    }

    status_init( to_oled, to_ticled );
    nvs_initialise();    // required for wifi driver
    wifi_task_start( );

    oled_task_start( to_oled );
    ticled_start_task( to_ticled );

    uart_task_start( to_decoder );
    tic_decode_start_task( to_decoder, to_mqtt, to_ticled, to_oled );
    mqtt_task_start( to_mqtt );
    clock_task_start( );
}
