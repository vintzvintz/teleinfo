
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"

#include "esp_netif.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "tic_types.h"
#include "event_loop.h"


static const char *TAG = "event_loop.c";

// Status event definitions 
ESP_EVENT_DEFINE_BASE(STATUS_EVENTS);

static esp_event_loop_handle_t s_status_evt_loop = NULL;

static tic_error_t post_integer (int value, int32_t evt_id)
{
    esp_err_t err = esp_event_post_to(s_status_evt_loop, 
            STATUS_EVENTS, evt_id, 
            &value, sizeof(value), 
            portMAX_DELAY);
    if (err != ESP_OK)
    {
        ESP_LOGE( TAG, "esp_event_post_to( <int> ) erreur %#02x", err);
        return TIC_ERR;
    }
    return TIC_OK;
}

static tic_error_t post_string (const char *str, int32_t evt_id)
{
    esp_err_t err = esp_event_post_to (s_status_evt_loop,
            STATUS_EVENTS, evt_id, 
            str, strlen(str)+1, 
            portMAX_DELAY);
    if (err != ESP_OK)
    {
        ESP_LOGE( TAG, "esp_event_post_to( <string> ) erreur %#02x", err);
        return TIC_ERR;
    }
    return TIC_OK;
}

static tic_error_t post_tic_data (const tic_data_t *data, int32_t evt_id)
{
    esp_err_t err = esp_event_post_to (s_status_evt_loop,
            STATUS_EVENTS, evt_id, 
            data, sizeof(*data), 
            portMAX_DELAY);
    if (err != ESP_OK)
    {
        ESP_LOGE( TAG, "esp_event_post_to( <tic_data> ) erreur %#02x", err);
        return TIC_ERR;
    }
    return TIC_OK;
}

tic_error_t send_event_baudrate (int baudrate)
{
    ESP_LOGD (TAG, "status_update_baudrate(%d)", baudrate);
    return post_integer( baudrate, STATUS_EVENT_BAUDRATE );
}

tic_error_t send_event_tic_data( const tic_data_t *data)
{
    if( !data )
    {
        ESP_LOGE( TAG, "send_event_tic_data() recoit NULL");
        return TIC_ERR_BAD_DATA;
    }
    ESP_LOGD (TAG, "send_event_tic_data() mode %d", data->mode );
    return post_tic_data (data, STATUS_EVENT_TIC_DATA);
}

tic_error_t send_event_wifi (const char* ssid)
{
    ESP_LOGD (TAG, "status_update_wifi() ssid='%s'", ssid);
    return post_string( ssid, STATUS_EVENT_WIFI);
}

tic_error_t send_event_mqtt (const char *mqtt_status)
{
    ESP_LOGD (TAG, "status_update_mqtt(%s)", mqtt_status);
    return post_string (mqtt_status, STATUS_EVENT_MQTT);
}

tic_error_t send_event_clock( const char* time_str)
{
  //  ESP_LOGD (TAG, "status_update_mqtt(%s)", time_str);
    return post_string (time_str, STATUS_EVENT_CLOCK_TICK);
}


// ********************************************
// Modèle pour les event_handlers
// ********************************************
static void event_baudrate (int baudrate) { ESP_LOGD( TAG, "STATUS_EVENT_BAUDRATE baudrate=%d", baudrate); }
static void event_tic_data ( const tic_data_t *data ) { ESP_LOGD( TAG, "STATUS_EVENT_TIC_DATA mode=%#02x", data->mode); }
static void event_clock_tick (const char *time_str) {  ESP_LOGV( TAG, "STATUS_EVENT_CLOCK_TICK %s", time_str); }
static void event_wifi (const char *ssid) { ESP_LOGD( TAG, "STATUS_EVENT_WIFI ssid='%s'", ssid ); }
static void event_mqtt( const char* status ) { ESP_LOGD( TAG, "STATUS_EVENT_MQTT %s", status); }

static void status_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    assert ( event_base==STATUS_EVENTS);
    switch( event_id )
    {
        case STATUS_EVENT_BAUDRATE:
            event_baudrate (*(int*)event_data);
            break;
        case STATUS_EVENT_TIC_DATA:
            event_tic_data ((tic_data_t *)event_data);
            break;
        case STATUS_EVENT_CLOCK_TICK:
            event_clock_tick ((const char *)event_data);
            break;
        case STATUS_EVENT_WIFI:
            event_wifi ((const char *)event_data);
            break;
        case STATUS_EVENT_MQTT:
            event_mqtt ((const char *)event_data);
            break;
        case STATUS_EVENT_NONE:
        default:
            ESP_LOGW( TAG, "STATUS_EVENT_NONE ou invalide");
    }
}

// enregistre un handler pour les STATUS_EVENT
tic_error_t tic_register_event_handler (int32_t event_id, esp_event_handler_t handler_func, void* handler_arg
                                          /*, esp_event_handler_instance_t* handler_ctx_arg */) {
    esp_err_t err = esp_event_handler_instance_register_with( s_status_evt_loop, 
                                                            STATUS_EVENTS,
                                                            event_id,
                                                            handler_func,
                                                            handler_arg,
                                                            NULL);
    if (err!=ESP_OK)
    {
        ESP_LOGD (TAG, "status_register_event_handler() erreur %#02x", err);
        return TIC_ERR;
    }
    return TIC_OK;
}


tic_error_t event_loop_init()
{
    esp_err_t esp_err;

    esp_event_loop_args_t loop_args = {
        .queue_size = 5,
        .task_name = "status_evt_loop_task", // task will be created
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = 3072,
    };

    // Create the event loop
    esp_err = esp_event_loop_create(&loop_args, &s_status_evt_loop);
    if ( esp_err != ESP_OK)
    {
        ESP_LOGE (TAG, "esp_event_loop_create() erreur %#02x", esp_err);
        return TIC_ERR_APP_INIT;
    }

    // handler pour les STATUS_EVENT
    tic_error_t tic_err = tic_register_event_handler( ESP_EVENT_ANY_ID, &status_event_handler, NULL );
    if ( tic_err != TIC_OK)
    {
        return TIC_ERR_APP_INIT;   // message loggué par status_register_event_handler()
    }

    return pdTRUE;
}
