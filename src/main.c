

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "mqtt_client.h"

#include "esp_log.h"


#include "tic_decode.h"
#include "uart_events.h"
#include "wifi.h"
#include "mqtt.h"


static const char *TAG = "main_app";


#define UART_STREAMBUFFER_SIZE 512
#define UART_STREAMBUFFER_TRIGGER 16




void blink_led_task( void *pvParams)
{
    gpio_set_direction( GPIO_NUM_4,GPIO_MODE_OUTPUT );
    for(;;)
    {
        gpio_set_level( GPIO_NUM_4, 1 );
        vTaskDelay( 300 / portTICK_PERIOD_MS );
        gpio_set_level( GPIO_NUM_4, 0 );
        vTaskDelay( 700 / portTICK_PERIOD_MS );
    }
}

void blink_led_start_task(void)
{
    xTaskCreate(blink_led_task, "blink_led", 2048, NULL, 12, NULL);
}


void app_main(void)
{
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    // transfere le flux de données brutes depuis l'UART vers le decodeur
    StreamBufferHandle_t uart_to_decoder = xStreamBufferCreate( UART_STREAMBUFFER_SIZE, UART_STREAMBUFFER_TRIGGER );
    if( uart_to_decoder == NULL )
    {
        ESP_LOGE( TAG, "Failed to create StreamBuffer" );
    }


    // transfere les trames depuis le decodeur vers le client MQTT pour publication
     QueueHandle_t decoder_to_mqtt = xQueueCreate( 5, sizeof( tic_dataset_t * ) );
    if( decoder_to_mqtt == NULL )
    {
        ESP_LOGE( TAG, "Failed to create Queue" );
    }

/*
    UBaseType_t nb_msg = uxQueueMessagesWaiting(decoder_to_mqtt  );
    ESP_LOGI( TAG, "decoder_to_mqtt %p : %d msg en attente", decoder_to_mqtt, nb_msg );
*/

    nvs_initialise();
    wifi_initialise();


    mqtt_task_start( decoder_to_mqtt );
    tic_decode_start_task( uart_to_decoder, decoder_to_mqtt );
    uart_task_start( uart_to_decoder );




    // blink_led_start_task();
}
