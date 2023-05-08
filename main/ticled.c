#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_event.h"


#include "tic_types.h"
#include "event_loop.h"          // pour status_register_event_handler()
#include "ticled.h"

static const char *TAG = "ticled.c";

// from KConfig
#define LED_GPIO     CONFIG_TIC_LED_GPIO    // GPIO_NUM_3

// Durée des clingotements
#define BLINK_COURT_ON    200         // ms
#define BLINK_COURT_OFF   200         // ms
#define BLINK_LONG_ON     2000        // ms
#define BLINK_LONG_OFF    2000        // ms

// Contrôle de la led du PtiInfo avec un EventGroup
EventGroupHandle_t s_ticled_events = NULL;
//#define BIT_BLINK_COURT    ( 1 << 0 )
#define BIT_BLINK_LONG     ( 1 << 1 )


static void status_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    assert (event_base == STATUS_EVENTS);
    assert (event_id == STATUS_EVENT_TIC_MODE );
    if( !s_ticled_events )
    {
        return;
    }

    tic_mode_t mode = *(tic_mode_t *)event_data;
    if ( mode != TIC_MODE_INCONNU )    // Trame complète decodée correctement
    {
        xEventGroupSetBits( s_ticled_events, BIT_BLINK_LONG );
    } else {
        xEventGroupClearBits( s_ticled_events, BIT_BLINK_LONG );
    }
}


static void ticled_task( void *pvParams )
{
    for(;;)
    {
        //  allume la led
        gpio_set_level( LED_GPIO, 1 );

        // clignote long ou court
        EventBits_t uxBits = xEventGroupGetBits( s_ticled_events );
        if( ( uxBits & BIT_BLINK_LONG) != 0 )
        {
            ESP_LOGD( TAG, "long blink" );
            vTaskDelay( BLINK_LONG_ON / portTICK_PERIOD_MS );
            gpio_set_level( LED_GPIO, 0 );
            vTaskDelay( BLINK_LONG_OFF/ portTICK_PERIOD_MS );
        }
        else
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
    err = tic_register_event_handler (STATUS_EVENT_TIC_MODE, status_event_handler, NULL);
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

