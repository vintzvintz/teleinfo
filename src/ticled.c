#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "driver/gpio.h"
#include "mqtt_client.h"

#include "esp_log.h"
#include "pinout.h"
#include "ticled.h"

static const char *TAG = "led_blink";


/*
 * Contrôle de la led du PtiInfo avec un EventGroup
 */
#define TIC_BIT_COURT    ( 1 << 0 )
#define TIC_BIT_LONG     ( 1 << 1 )


typedef struct led_task_params_s {
    EventGroupHandle_t blink_events;
} led_task_params_t;



void ticled_blink_short(EventGroupHandle_t to_ticled ) 
{
     xEventGroupSetBits( to_ticled, TIC_BIT_COURT );
}

void ticled_blink_long(EventGroupHandle_t to_ticled ) 
{
    // blink led at each UART receive event
    xEventGroupSetBits( to_ticled, TIC_BIT_LONG );
}


static void ticled_task( void *pvParams )
{
    led_task_params_t *params = (led_task_params_t *)pvParams;

    gpio_set_direction( TIC_GPIO_LED, GPIO_MODE_OUTPUT );
    gpio_set_level( TIC_GPIO_LED, 0 );

    for(;;)
    {
        EventBits_t uxBits;

        // Wait a maximum of 100ms for either bit 0 or bit 4 to be set within
        // the event group.  Clear the bits before exiting.
        uxBits = xEventGroupWaitBits(
                    params->blink_events,        // The event group being tested.
                    TIC_BIT_COURT | TIC_BIT_LONG,              // The bits within the event group to wait for.
                    pdTRUE,         // BITs should be cleared before returning.
                    pdFALSE,        // Don't wait for both bits, either bit will do.
                    portMAX_DELAY ); // Wait a max

        //  allume la led
        gpio_set_level( TIC_GPIO_LED, 1 );

        // laisse ON pendant une durée supérieure à 1 trame ( prioritaire sur BLINK_COURT )
        if( ( uxBits & TIC_BIT_LONG ) != 0 )
        {
            ESP_LOGD( TAG, "long blink" );
            vTaskDelay( TIC_BLINK_LONG / portTICK_PERIOD_MS );
        }
        else if( ( uxBits & TIC_BIT_COURT ) != 0 )
        {
            ESP_LOGD( TAG, "short blink" );
            vTaskDelay( TIC_BLINK_COURT / portTICK_PERIOD_MS );
        }

        //  Etient la led
        gpio_set_level( TIC_GPIO_LED, 0 );
    }
}

void ticled_start_task( EventGroupHandle_t blink_events )
{
    led_task_params_t *params = malloc( sizeof(led_task_params_t) );
    if( params ==NULL )
    {
        ESP_LOGE( TAG, "Malloc failed" );
        return;
    }

    params->blink_events = blink_events;

    xTaskCreate(ticled_task, "ticled", 4096, params, 1, NULL);

}

