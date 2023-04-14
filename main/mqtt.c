/* Intellisense bullshit */
#undef __linux__

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "mqtt_client.h"

#include "errors.h"
#include "process.h"
#include "status.h"
#include "mqtt.h"

static const char *TAG = "mqtt.c";

typedef struct mqtt_task_param_s {
    //QueueHandle_t from_decoder;
    esp_mqtt_client_handle_t esp_client;
} mqtt_task_param_t;


static QueueHandle_t s_to_mqtt = NULL;


static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

// pour le debug
//static int32_t s_allocated_msg = 0;

// Alloue/libere un mqtt_msg_t  
// pour la communication entre tâches process.c et mqtt.c
mqtt_msg_t * mqtt_msg_alloc()
{
    mqtt_msg_t *msg = calloc( 1, sizeof(mqtt_msg_t) );
    char *topic = calloc( 1, MQTT_TOPIC_BUFFER_SIZE );
    char *payload = calloc( 1, MQTT_PAYLOAD_BUFFER_SIZE );

    if ( msg==NULL ||topic==NULL || payload==NULL )
    {
        ESP_LOGE( TAG, "mqtt_msg_alloc() failed (out of memory ?)");
        free(topic);
        free(payload);
        free(msg);
        return NULL;
    }
    msg->topic = topic;
    msg->payload = payload;
    //s_allocated_msg += 1;
    // ESP_LOGD( TAG, "mqtt_msg_alloc()=%p   nb_msg_alloues=%"PRIi32, msg, s_allocated_msg );
    return msg;
}

void mqtt_msg_free(mqtt_msg_t *msg)
{
    if( msg == NULL)
        return;

    if( msg->topic != NULL )
    { 
        free( msg->topic);
        msg->topic = NULL;
    }
    if( msg->payload != NULL )
    { 
        free( msg->payload);
        msg->payload = NULL;
    }
    free( msg );
    
    //s_allocated_msg -= 1;
}


tic_error_t mqtt_receive_msg( mqtt_msg_t *msg )
{
    //ESP_LOGD( TAG, " mqtt_receive_msg()");
    if( s_to_mqtt == NULL )
    {
        ESP_LOGD( TAG, "queue mqtt pas initialisée" );
        return TIC_ERR;
    }

    BaseType_t send_ok = xQueueSend( s_to_mqtt, &msg, 10 );
    if( send_ok != pdTRUE )
    {
        ESP_LOGE( TAG, "message refusé par mqtt_task (queue pleine)" );
        return TIC_ERR_QUEUEFULL;
    }
    return TIC_OK;
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
    //ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
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


void mqtt_task( void *pvParams )
{
    ESP_LOGI( TAG, "mqtt_task()");

    mqtt_task_param_t *params = (mqtt_task_param_t *)pvParams;

    // demarre le client MQTT fourni avec EDF-IDF
    if( params->esp_client )
    {
        esp_err_t err = esp_mqtt_client_start(params->esp_client);
        if( err != ESP_OK ) {
            ESP_LOGE( TAG, "esp_mqtt_client_start() erreur %d", err);
        }
    }

    TickType_t max_ticks = MQTT_TIC_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS; 
    mqtt_msg_t *msg = NULL;
    for(;;)
    {
        mqtt_msg_free(msg);    // libère les buffers alloués par process_task
        msg = NULL;

        BaseType_t msg_received = xQueueReceive( s_to_mqtt, &msg, max_ticks );
        if( msg_received != pdTRUE )
        {
            ESP_LOGI( TAG, "Aucun message MQTT à envoyer depuis %d secondes", TIC_PROCESS_TIMEOUT );
            continue;
        }
        if( msg==NULL )
        {
            ESP_LOGD( TAG, "Message MQTT null");
            continue;
        }
        if( msg->topic==NULL || msg->topic[0]=='\0' )
        {
            ESP_LOGD( TAG, "Topic MQTT absent");
            continue;
        }
        if( msg->payload==NULL || msg->payload[0]=='\0' )
        {
            ESP_LOGD( TAG, "Payload MQTT absent");
            continue;
        }

        ESP_LOGD( TAG, "dummy_mqtt topic\n%s", msg->topic );
        ESP_LOGD( TAG, "dummy_mqtt payload\n%s", msg->payload );

        if( params->esp_client )
        {
            esp_mqtt_client_publish( params->esp_client, msg->topic, msg->payload, 0, 0, 0);
            // ingore les erreurs
        }
    }

    ESP_LOGE( TAG, "fatal: mqtt_task exited" );
    esp_mqtt_client_destroy( params->esp_client );
    vTaskDelete(NULL);
}

static const uint8_t psk_key[] = PSK_KEY ;

static const psk_hint_key_t psk_hint_key = {
        .key = psk_key,
        .key_size = sizeof(psk_key),
        .hint = PSK_IDENTITY
    };

static void log_mqtt_cfg( esp_mqtt_client_config_t cfg )
{
    ESP_LOGI( TAG, "mqtt_broker_hostname: %s", cfg.broker.address.hostname );
    ESP_LOGI( TAG, "mqtt_broker_port: %"PRIi32, cfg.broker.address.port );
    ESP_LOGI( TAG, "mqtt_broker_transport: %d", cfg.broker.address.transport );
    ESP_LOGI( TAG, "mqtt_psk_identity: %s", cfg.broker.verification.psk_hint_key->hint );
    ESP_LOGI( TAG, "mqtt_psk_key_size: %d", cfg.broker.verification.psk_hint_key->key_size );
    for( int i=0;  i < cfg.broker.verification.psk_hint_key->key_size; i++ )
    {
        ESP_LOGI( TAG, "mqtt_psk_key[%d]: %#x", i, cfg.broker.verification.psk_hint_key->key[i] );
    }
}


BaseType_t mqtt_task_start( int dummy )
{
    //esp_log_level_set("*", ESP_LOG_INFO);
    //esp_log_level_set( TAG, ESP_LOG_INFO );
    //esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    //esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    //esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    //esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    //esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    esp_mqtt_client_handle_t client = NULL;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = BROKER_HOST,
        .broker.address.port = BROKER_PORT,
        .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
        .broker.verification.psk_hint_key = &psk_hint_key,
    };

    if( !dummy )
    {
        log_mqtt_cfg( mqtt_cfg );
        client = esp_mqtt_client_init(&mqtt_cfg);
        if( client == NULL )
        {
            ESP_LOGE( TAG, "esp_mqtt_client_init() failed");
            return pdFALSE;
        }
    
        esp_err_t err;
        // The last argument may be used to pass data to the event handler, in this example mqtt_event_handler 
        err = esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL );
        if( err != ESP_OK ) {
            ESP_LOGE( TAG, "esp_mqtt_client_register_event() erreur %d", err);
            return pdFALSE;
        }
    }

    // passe le client_handle à la tâche
    mqtt_task_param_t *task_params = calloc( 1, sizeof( mqtt_task_param_t ) );
    if( task_params==NULL )
    {
        ESP_LOGE( TAG, "calloc() failed" );
        return pdFALSE;
    }
    task_params->esp_client = client;

    // Queue pour recevoir les messages formattés à publier
    s_to_mqtt = xQueueCreate( 5, sizeof( mqtt_msg_t * ) );
    if( s_to_mqtt==NULL )
    {
        ESP_LOGE( TAG, "xCreateQueue() failed" );
        return pdFALSE;
    }

    // create mqtt client task
    BaseType_t task_created = xTaskCreate( mqtt_task, "mqtt_task", 4096, task_params, 12, NULL);
    if( task_created != pdPASS )
    {
        ESP_LOGE( TAG, "xTaskCreate() failed");
    }
    return task_created;
}


/*
void mqtt_task_dummy( void *pvParams )
{
    ESP_LOGI( TAG, "mqtt_task_dummy()");

    TickType_t max_ticks = MQTT_TIC_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS; 
    mqtt_msg_t *msg = NULL;
    for(;;)
    {
        mqtt_msg_free(msg);    // libère les buffers alloués par process_task
        msg = NULL;

        BaseType_t msg_received = xQueueReceive( s_to_mqtt, &msg, max_ticks );
        if( msg_received != pdTRUE )
        {
            ESP_LOGI( TAG, "Aucun message MQTT à envoyer depuis %d secondes (%p)", TIC_PROCESS_TIMEOUT, msg );
            continue;
        }
        if( msg==NULL )
        {
            ESP_LOGD( TAG, "Message MQTT null");
            continue;
        }
        if( msg->topic==NULL || msg->topic[0]=='\0' )
        {
            ESP_LOGD( TAG, "Topic MQTT absent");
            continue;
        }
        if( msg->payload==NULL || msg->payload[0]=='\0' )
        {
            ESP_LOGD( TAG, "Payload MQTT absent");
            continue;
        }

        ESP_LOGD( TAG, "dummy_mqtt topic\n%s", msg->topic );
        ESP_LOGD( TAG, "dummy_mqtt payload\n%s", msg->payload );

    }
    ESP_LOGE( TAG, "fatal: mqtt_task exited" );
    vTaskDelete(NULL);
}
*/
/*
BaseType_t mqtt_dummy_task_start( )
{
    esp_log_level_set( TAG, ESP_LOG_DEBUG );


    // Queue pour recevoir les messages formattés à publier
    s_to_mqtt = xQueueCreate( 5, sizeof( mqtt_msg_t * ) );
    if( s_to_mqtt==NULL )
    {
        ESP_LOGE( TAG, "xCreateQueue() failed" );
        return pdFALSE;    // inutile de continuer....
    }

    // create mqtt client task
    BaseType_t task_created = xTaskCreate( mqtt_task_dummy, "mqtt_task_dummy", 2048, NULL, 12, NULL);
    if( task_created != pdPASS )
    {
        ESP_LOGE( TAG, "xTaskCreate() failed");
    }
    return task_created;
}
*/