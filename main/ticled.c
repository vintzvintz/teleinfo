#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_event.h"


#include "tic_types.h"
//#include "tic_config.h"     // led GPIO pin
#include "status.h"          // pour status_register_event_handler()
#include "ticled.h"

static const char *TAG = "ticled.c";

// from KConfig
#define LED_GPIO     CONFIG_TIC_LED_GPIO    // GPIO_NUM_3

// Durée des clingotements
#define BLINK_COURT_ON    250         // ms
#define BLINK_COURT_OFF   100         // ms
#define BLINK_LONG_ON     2000        // ms
#define BLINK_LONG_OFF    1000        // ms

// Contrôle de la led du PtiInfo avec un EventGroup
EventGroupHandle_t s_ticled_events = NULL;
#define BIT_BLINK_COURT    ( 1 << 0 )
#define BIT_BLINK_LONG     ( 1 << 1 )


static void ticled_blink( const EventBits_t bits ) 
{
    if( s_ticled_events == NULL )
    {
        ESP_LOGD( TAG, "s_ticled_events pas initialisé" );
        return;
    }
    xEventGroupSetBits( s_ticled_events, bits );
}


static void status_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    assert (event_base == STATUS_EVENTS);
    switch (event_id)
    {
        case STATUS_EVENT_BAUDRATE:    // Donnees reçues par l'UART
            ticled_blink( BIT_BLINK_COURT );
            break;
        case STATUS_EVENT_TIC_MODE:    // Trame complète decodée correctement
            ticled_blink( BIT_BLINK_LONG );
            break;
        // default:
            // ignore autres evènements
    }
}


static void ticled_task( void *pvParams )
{
    for(;;)
    {
        EventBits_t uxBits = xEventGroupWaitBits(
                    s_ticled_events,
                    (BIT_BLINK_COURT | BIT_BLINK_LONG),              // The bits within the event group to wait for.
                    pdTRUE,         // BITs should be cleared before returning.
                    pdFALSE,        // Don't wait for both bits, either bit will do.
                    portMAX_DELAY ); // Wait a max

        //  allume la led
        gpio_set_level( LED_GPIO, 1 );

        // clignote long ou court
        if( ( uxBits & BIT_BLINK_LONG) != 0 )
        {
            ESP_LOGD( TAG, "long blink" );
            vTaskDelay( BLINK_LONG_ON / portTICK_PERIOD_MS );
            gpio_set_level( LED_GPIO, 0 );
            vTaskDelay( BLINK_LONG_OFF/ portTICK_PERIOD_MS );
        }
        else if( ( uxBits & BIT_BLINK_COURT ) != 0 )
        {
            ESP_LOGD( TAG, "short blink" );
            vTaskDelay( BLINK_COURT_ON / portTICK_PERIOD_MS );
            gpio_set_level( LED_GPIO, 0 );
            vTaskDelay( BLINK_COURT_OFF/ portTICK_PERIOD_MS );
        }
    }
}

tic_error_t ticled_task_start()
{
    s_ticled_events = xEventGroupCreate();
    if( s_ticled_events == NULL )
    {
        ESP_LOGE( TAG, "xCreateQueue() failed" );
        return TIC_ERR_APP_INIT;
    }

    // init GPIO
    esp_err_t esp_err;
    esp_err = gpio_set_direction( LED_GPIO, GPIO_MODE_OUTPUT );
    if (esp_err != ESP_OK)
    {
        ESP_LOGE( TAG, "gpio_set_direction() erreur %#02x", esp_err);
        return TIC_ERR_APP_INIT;
    }
    esp_err = gpio_set_level( LED_GPIO, 0 );
    if (esp_err != ESP_OK)
    {
        ESP_LOGE( TAG, "gpio_set_levet() erreur %#02x", esp_err);
        return TIC_ERR_APP_INIT;
    }

    // enregistre un handler pour reecevoir les notifications BAUDRATE et TIC_MODE
    tic_error_t err;
    err = status_register_event_handler (ESP_EVENT_ANY_ID, status_event_handler, NULL);
    if (err != TIC_OK)
    {
        ESP_LOGE( TAG, "status_register_event_handler()) erreur %#02x", err);
        return TIC_ERR_APP_INIT;
    }

    if( xTaskCreate(ticled_task, "ticled", 4096, NULL, 1, NULL) != pdPASS )
    {
        ESP_LOGE (TAG, "xTaskCreate() failed");
        return TIC_ERR_APP_INIT;
    }
    return TIC_OK;
}

