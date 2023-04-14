#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
//#include "freertos/stream_buffer.h"
#include "driver/gpio.h"
//#include "mqtt_client.h"

#include "esp_log.h"
#include "pinout.h"
#include "ticled.h"

static const char *TAG = "ticled.c";

// Contrôle de la led du PtiInfo avec un EventGroup
EventGroupHandle_t s_to_ticled = NULL;
#define TIC_BIT_COURT    ( 1 << 0 )
#define TIC_BIT_LONG     ( 1 << 1 )

/*
typedef struct led_task_params_s {
    EventGroupHandle_t blink_events;
} led_task_params_t;
*/


static void ticled_blink( const EventBits_t bits ) 
{
    if( s_to_ticled == NULL )
    {
        ESP_LOGD( TAG, "s_to_ticled pas initialisé" );
        return;
    }
    xEventGroupSetBits( s_to_ticled, bits );
}

void ticled_blink_short( ) 
{
    ticled_blink( TIC_BIT_COURT );
}

void ticled_blink_long( ) 
{
    ticled_blink( TIC_BIT_LONG );
}


static void ticled_task( void *pvParams )
{
    // led_task_params_t *params = (led_task_params_t *)pvParams;
    
    gpio_set_direction( TIC_GPIO_LED, GPIO_MODE_OUTPUT );
    gpio_set_level( TIC_GPIO_LED, 0 );

    for(;;)
    {
        EventBits_t uxBits;

        // Wait a maximum of 100ms for either bit 0 or bit 4 to be set within
        // the event group.  Clear the bits before exiting.
        uxBits = xEventGroupWaitBits(
                    s_to_ticled,        // The event group being tested.
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

BaseType_t ticled_task_start()
{
    s_to_ticled = xEventGroupCreate();
    if( s_to_ticled == NULL )
    {
        ESP_LOGE( TAG, "xCreateQueue() failed" );
        return pdFALSE;
    }

    //led_task_params_t *params = malloc( sizeof(led_task_params_t) );
    //if( params ==NULL )
    ///{
    //    ESP_LOGE( TAG, "Malloc failed" );
    //    return;
    //}
    // params->blink_events = blink_events;

    return xTaskCreate(ticled_task, "ticled", 4096, NULL, 1, NULL);

}

