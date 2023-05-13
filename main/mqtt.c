
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "mqtt_client.h"

#include "tic_types.h"
#include "tic_config.h"
#include "event_loop.h"
#include "mqtt.h"
#include "nvs_utils.h"

static const char *TAG = "mqtt.c";


esp_mqtt_client_handle_t s_esp_client = NULL;
EventGroupHandle_t s_client_evt_group = NULL;
#define BIT_CLIENT_RESTART   BIT0

// messages à envoyer
static QueueHandle_t s_to_mqtt = NULL;


// paramètres de connexion au broker mqtt
static psk_hint_key_t s_psk_hint_key = {0};
static esp_mqtt_client_config_t s_mqtt_cfg = {
        .broker.verification.psk_hint_key = &s_psk_hint_key
};

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
        send_event_mqtt ("connecting...");
        break;
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED to %s", (s_mqtt_cfg.broker.address.uri) );
        send_event_mqtt( "connected" );
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        send_event_mqtt( "connecting..." );
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
        send_event_mqtt( "error" );
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}


static void log_mqtt_cfg( const esp_mqtt_client_config_t *cfg )
{
    ESP_LOGD( TAG, "mqtt_broker_uri: %s", 
        (cfg->broker.address.uri ? cfg->broker.address.uri : "NULL") );
    ESP_LOGD( TAG, "mqtt_psk_identity: %s", 
        (cfg->broker.verification.psk_hint_key->hint ? cfg->broker.verification.psk_hint_key->hint : "NULL") );
    ESP_LOGD( TAG, "mqtt_psk_key_size: %d", cfg->broker.verification.psk_hint_key->key_size );
    for( int i=0;  i < cfg->broker.verification.psk_hint_key->key_size; i++ )
    {
        ESP_LOGD( TAG, "mqtt_psk_key[%d]: %#x", i, cfg->broker.verification.psk_hint_key->key[i] );
    }
}


static tic_error_t get_mqtt_config_from_nvs (esp_mqtt_client_config_t *cfg)
{
    tic_error_t err;
    struct address_t *addr = &(s_mqtt_cfg.broker.address);
    struct psk_key_hint *hint_key = (struct psk_key_hint *)(cfg->broker.verification.psk_hint_key);

    // URI du broker MQTT
    if(addr->uri)
    {
        free((void*)(addr->uri));    // cast en void* pour éviter un warning sur type (const char*)
        addr->uri = NULL;
    }
    err = console_nvs_get_string( TIC_NVS_MQTT_BROKER, (char **)(&(addr->uri)) );
    if (err != TIC_OK)
    {
        ESP_LOGE (TAG, "URI du broker MQTT non configurée");
        return err;
    }

    // preshared key
    if (hint_key->hint)
    {
        free((void*)(hint_key->hint));
        hint_key->hint = NULL;
    }
    err = console_nvs_get_string( TIC_NVS_MQTT_PSK_ID, (char **)(&(hint_key->hint)) );
    if (err != TIC_OK)
    {
        ESP_LOGE (TAG, "Identité de la clé PSK non configurée");
        return err;
    }

    if (hint_key->key)
    {
        free((void*)(hint_key->key));
        hint_key->key = NULL;
    }
    err = console_nvs_get_blob( TIC_NVS_MQTT_PSK_KEY, (char **)(&(hint_key->key)), (size_t *)(&(hint_key->key_size)) );
    if (err != TIC_OK)
    {
        ESP_LOGE (TAG, "Valeur de la clé PSK non configurée");
        return err;
    }
    log_mqtt_cfg (cfg);
    return TIC_OK;
}


// initialise le client MQTT au lancement et lorsque les parametres changent
static void mqtt_client_task( void *pvParams )
{
    ESP_LOGD( TAG, "mqtt_client_task()");

    tic_error_t tic_err;
    esp_err_t esp_err;
    for(;;)
    {
        // attend un signal pour recommencer la sequence d'initialisation
        /*EventBits_t bits = */ xEventGroupWaitBits(s_client_evt_group, (BIT_CLIENT_RESTART), pdTRUE, pdFALSE, portMAX_DELAY);

        // recupère les parametres de connexion dans le NVS
        tic_err = get_mqtt_config_from_nvs (&s_mqtt_cfg);
        if (tic_err != TIC_OK)
        {
            // inutile de continuer si les paramètres sont incorrects
            continue;   // msg d'erreur loggué par update_mqtt_config()
        }

        // cree un nouveau client et efface l'ancien
        if (s_esp_client)
        {
            if( esp_mqtt_client_destroy (s_esp_client) == ESP_OK )
            {
                ESP_LOGD( TAG, "client mqtt destroyed" );
                s_esp_client = NULL;
            }
        }
        s_esp_client = esp_mqtt_client_init (&s_mqtt_cfg);
        if(!s_esp_client)
        {
            ESP_LOGE( TAG, "esp_mqtt_client_init() failed");
            continue;
        }

        // enregistre le handler
        esp_err = esp_mqtt_client_register_event(s_esp_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL );
        if( esp_err != ESP_OK )
        {
            ESP_LOGE( TAG, "esp_mqtt_client_register_event() erreur %d", esp_err);
            continue;
        }

        // demarre le client MQTT 
        esp_err = esp_mqtt_client_start(s_esp_client);
        if( esp_err != ESP_OK )
        {
            ESP_LOGE( TAG, "esp_mqtt_client_start() erreur %d", esp_err);
            continue;
        }
        ESP_LOGI( TAG, "client mqtt started");
    }

    ESP_LOGE( TAG, "fatal: mqtt_client_task exited" );
    esp_mqtt_client_destroy( s_esp_client );
    vTaskDelete(NULL);
}


static void mqtt_publish_task( void *pvParams )
{
    ESP_LOGI( TAG, "mqtt_publish_task()");

    //TickType_t max_ticks = MQTT_TIC_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS; 
    mqtt_msg_t *msg = NULL;
    for(;;)
    {
        mqtt_msg_free(msg);    // libère les buffers alloués par process_task
        msg = NULL;

        BaseType_t msg_received = xQueueReceive( s_to_mqtt, &msg, portMAX_DELAY );
        if( msg_received != pdTRUE )
        {
            continue;   // timeout
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

        ESP_LOGD( TAG, "mqtt topic = %s", msg->topic );
        ESP_LOGD( TAG, "mqtt payload = %s", msg->payload );

        if( s_esp_client )
        {
            esp_mqtt_client_publish( s_esp_client, msg->topic, msg->payload, 0, 0, 0);
            // ignore erreurs
        }
    }
    ESP_LOGE( TAG, "fatal: mqtt_publish_task exited" );
    vTaskDelete(NULL);
}



tic_error_t mqtt_client_restart()
{
    xEventGroupSetBits( s_client_evt_group, BIT_CLIENT_RESTART);
    return TIC_OK;
}


tic_error_t mqtt_task_start( int dummy )
{
    // Queue pour recevoir les messages formattés à publier
    s_to_mqtt = xQueueCreate( 5, sizeof( mqtt_msg_t * ) );
    if( s_to_mqtt==NULL )
    {
        ESP_LOGE( TAG, "xCreateQueue() failed" );
        return TIC_ERR_APP_INIT;
    }

    // event group pour demander un redemarrage du client mqtt
    s_client_evt_group = xEventGroupCreate();
    if( s_client_evt_group==NULL )
    {
        ESP_LOGE( TAG, "xEventGroupCreate() failed" );
        return TIC_ERR_APP_INIT;
    }

    BaseType_t task_created;

    // en mode dummy, le client ne s'executera pas et s_esp_client restera NULL à tout jamais
    if( !dummy )
    {

        task_created = xTaskCreate( mqtt_client_task, "mqtt_client", 4096, /*task_params*/ NULL, 12, NULL);
        if( task_created != pdPASS )
        {
            ESP_LOGE( TAG, "xTaskCreate() failed");
            return TIC_ERR_APP_INIT;
        }
        mqtt_client_restart();    // lance le client à la création de la tâche
    }

    // create mqtt publish task
    task_created = xTaskCreate( mqtt_publish_task, "mqtt_publish", 4096, /*task_params*/ NULL, 12, NULL);
    if( task_created != pdPASS )
    {
        ESP_LOGE( TAG, "xTaskCreate() failed");
        return TIC_ERR_APP_INIT;
    }

    return TIC_OK;
}
