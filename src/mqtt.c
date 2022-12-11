
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "tic_decode.h"
#include "status.h"
#include "mqtt.h"

static const char *TAG = "mqtt_task";

#define TIC_BROKER_URL CONFIG_TIC_BROKER_URL

// données à publier 
const char *PUBLISHED_DATA[] = { "ADCO", "IINST", "PTEC", "BASE", "HCHC", "HCHP", "PAPP" };
const size_t PUBLISEHD_DATA_COUNT = sizeof(PUBLISHED_DATA) / sizeof(PUBLISHED_DATA[0]);

// données de type numerique
// TODO : detection automatique avec strtol
const char *NUMERIC_DATA[] = { "IINST", "BASE", "HCHC", "HCHP", "PAPP" };
const size_t NUMERIC_DATA_COUNT = sizeof(NUMERIC_DATA) / sizeof(NUMERIC_DATA[0]);


typedef struct mqtt_task_param_s {
    QueueHandle_t from_decoder;
    QueueHandle_t to_oled;
    esp_mqtt_client_handle_t esp_client;
} mqtt_task_param_t;


typedef struct mqtt_handler_ctx_s {
    QueueHandle_t to_oled;
} mqtt_handler_ctx_t;


static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}


/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "MQTT_EVENT_BEFORE CONNECT");
        status_mqtt_update( "connecting..." );
        break;
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        status_mqtt_update( "connected" );
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        status_mqtt_update( "connecting..." );
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        status_mqtt_update( "error" );
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}


static int is_label_in_list( const char *label, const char *list[], size_t nb )
{
    for( int i=0; i<nb; i++ )
    {
        if( strcmp( label, list[i] ) == 0 )
        {
            return i;
        }
    }
    return -1;
}


static int is_label_published( const char *label )
{
    return is_label_in_list( label, PUBLISHED_DATA, PUBLISEHD_DATA_COUNT ) >= 0;
}

static int is_label_integer( const char *label )
{
    return is_label_in_list( label, NUMERIC_DATA, NUMERIC_DATA_COUNT ) >= 0;
}


static const char *FORMAT_STRING_SANS_HORODATE = "  {\"lbl\": \"%s\", \"val\": \"%s\" }";
static const char *FORMAT_STRING_AVEC_HORODATE = "  {\"lbl\": \"%s\", \"ts\": \"%s\", \"val\": \"%s\" }";
static const char *FORMAT_NUMERIC_SANS_HORODATE = "  {\"lbl\": \"%s\", \"val\": %d }";
static const char *FORMAT_NUMERIC_AVEC_HORODATE = "  {\"lbl\": \"%s\", \"ts\": \"%s\", \"val\": %d }";


static size_t printf_ds( char *buf, size_t size, const tic_dataset_t *ds )
{
    if( is_label_integer( ds->etiquette ) )
    {
        // formatte la valeur numerique avec ou sans horodate
        uint32_t val = strtol( ds->valeur, NULL, 10 );
        if( ds->horodate[0] == '\0' )
        {
            return snprintf( buf, size, FORMAT_NUMERIC_SANS_HORODATE, ds->etiquette, val);
        }
        else
        {
            return snprintf( buf, size, FORMAT_NUMERIC_AVEC_HORODATE, ds->etiquette, ds->horodate, val );
        }
    }
    else
    {
        // formatte la valeur texte avec ou sans horodate
        if( ds->horodate[0] == '\0' )
        {
            return snprintf( buf, size, FORMAT_STRING_SANS_HORODATE, ds->etiquette, ds->valeur);
        }
        else
        {
            return snprintf( buf, size, FORMAT_STRING_AVEC_HORODATE, ds->etiquette, ds->horodate, ds->valeur );
        }
    }
}


static mqtt_error_t datasets_to_json( char *buf, size_t size, const tic_dataset_t *ds )
{
    size_t pos = 0;

    pos += snprintf( &(buf[pos]), size-pos, "{ \"tic_frame\" : [\n" );

    while( ds!=NULL && (size-pos) > 0 )
    {
        // ignore les etiquettes non exportées 
        if( is_label_published( ds->etiquette ) == 0  )
        {
            ds = ds->next;
            continue;
        }

        // formatte la donnée en JSON
        pos += printf_ds( &(buf[pos]), size-pos, ds );
        if( pos >= (size-2) )     // -2 pour la virgule et le \n 
        {
            ESP_LOGE( TAG, "JSON buffer overflow" );
            return MQTT_ERR_OVERFLOW;
        }

        // separation entre les données publiées
        if( ds->next != NULL) 
        {
            buf[pos++] = ',';
        }
        buf[pos++] = '\n';

        // donnée suivante
        ds = ds->next;
    }

    // termine le tableau et l'objet JSON racine
    pos += snprintf( &(buf[pos]), size-pos, "] }\n" );
    if( pos > (size-1) )
    {
        ESP_LOGE( TAG, "JSON buffer overflow" );
        return MQTT_ERR_OVERFLOW;
    }

    return MQTT_OK;
}


void mqtt_task( void *pvParams )
{
    ESP_LOGI( TAG, "mqtt_task()");

    mqtt_task_param_t *params = (mqtt_task_param_t *)pvParams;

    // demarre le client MQTT fourni avec EDF-IDF
    esp_err_t err;
    err = esp_mqtt_client_start(params->esp_client);
    if( err != ESP_OK ) {
        ESP_LOGE( TAG, "esp_mqtt_client_start() erreur %d", err);
    }

    char *json_buffer = malloc(MQTT_JSON_BUFFER_SIZE);
    TickType_t max_ticks = MQTT_TIC_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS; 
    tic_dataset_t *datasets = NULL; 

    mqtt_error_t mqtt_err;
    for(;;)
    {
        tic_dataset_free( datasets );    ///libère la memoire allouée par tic_decode
        datasets = NULL;

        BaseType_t ds_received = xQueueReceive( params->from_decoder, &datasets, max_ticks );
        if( ds_received != pdTRUE )
        {
            ESP_LOGI( TAG, "Aucune trame téléinfo reçue depuis %d secondes", MQTT_TIC_TIMEOUT_SEC );
            continue;
        }

        mqtt_err = datasets_to_json( json_buffer, MQTT_JSON_BUFFER_SIZE, datasets );
        if( mqtt_err != MQTT_OK )
        {
            continue;
        }

        int msg_id = esp_mqtt_client_publish( params->esp_client, "/linky/pouet", json_buffer, 0, 0, 0);
        if( msg_id < 0 )
        {
            //ESP_LOGE( TAG, "Echec de la publication mqtt");
        }
    }

    ESP_LOGE( TAG, "fatal: mqtt_task exited" );
    free(json_buffer);
    esp_mqtt_client_destroy( params->esp_client );
    vTaskDelete(NULL);
}


BaseType_t mqtt_task_start( QueueHandle_t from_decoder, QueueHandle_t to_oled )
{
   esp_log_level_set( TAG, ESP_LOG_INFO );

   esp_mqtt_client_config_t mqtt_cfg = {
        .uri = TIC_BROKER_URL
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    if( client == NULL )
    {
        ESP_LOGE( TAG, "esp_mqtt_client_init() failed");
        return pdFALSE;
    }

    mqtt_handler_ctx_t *ctx = malloc( sizeof(mqtt_handler_ctx_t) );
    if( ctx == NULL)
    {
        ESP_LOGE( TAG, "malloc( mqtt_handler_ctx ) failed");
        return pdFALSE;
    }
    ctx->to_oled = to_oled;

    esp_err_t err;
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    err = esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, ctx );
    if( err != ESP_OK ) {
        ESP_LOGE( TAG, "esp_mqtt_client_register_event() erreur %d", err);
        return pdFALSE;
    }


    // setup inter-task communication stuff
    mqtt_task_param_t *task_params = malloc( sizeof( mqtt_task_param_t ) );
    task_params->from_decoder = from_decoder;
    task_params->to_oled = to_oled;
    task_params->esp_client = client;

    // create mqtt client task
    BaseType_t task_created = xTaskCreate( mqtt_task, "mqtt_task", 4096, task_params, 12, NULL);
    if( task_created != pdPASS )
    {
        ESP_LOGE( TAG, "xTaskCreate() failed");
    }
    return task_created;
}
