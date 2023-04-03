/* Intellisense bullshit */
#undef __linux__

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
#include "esp_tls.h"
#include "mqtt_client.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "decode.h"
#include "flags.h"
#include "status.h"
#include "mqtt.h"

static const char *TAG = "mqtt_task";


// données à publier 
//const char *PUBLISHED_DATA[] = { "IINST", "PTEC", "BASE", "HCHC", "HCHP", "PAPP" };
//const size_t PUBLISEHD_DATA_COUNT = sizeof(PUBLISHED_DATA) / sizeof(PUBLISHED_DATA[0]);

// données de type numerique
/*

// mode historique
const char *NUMERIC_DATA[] = { "IINST", "BASE", "HCHC", "HCHP", "PAPP", "IMAX", "ISOUSC" };
*/
//mode standard
//const char *NUMERIC_DATA[] = { "CCASN", "CCASN-1", "EAST", "IRMS1", "URMS1", "SINST", "SMAXSN", "SMAXSN-1", "UMOY" };

//const size_t NUMERIC_DATA_COUNT = sizeof(NUMERIC_DATA) / sizeof(NUMERIC_DATA[0]);

// identifiant du compteur


//static const char *LABEL_ADCO = "ADCO";
static const char *LABEL_ID_DEVICE = "ADSC";


typedef struct mqtt_task_param_s {
    QueueHandle_t from_decoder;
    esp_mqtt_client_handle_t esp_client;
} mqtt_task_param_t;

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

/*
static int find_label_in_list( const char *label, const char *list[], size_t nb )
{
    for( int i=0; i<nb; i++ )
    {
        if( strcmp( label, list[i] ) == 0 )
            return i;
    }
    return -1;
}

static int is_published( const tic_dataset_t *ds )
{
    return true;
    //return find_label_in_list( ds->etiquette, PUBLISHED_DATA, PUBLISEHD_DATA_COUNT ) >= 0;
}

static int is_integer( const tic_dataset_t *ds )
{
    return find_label_in_list(  ds->etiquette, NUMERIC_DATA, NUMERIC_DATA_COUNT ) >= 0;
}
*/

//static const char *FORMAT_STRING_SANS_HORODATE = "  {\"lbl\": \"%s\", \"val\": \"%s\" }";
//static const char *FORMAT_STRING_AVEC_HORODATE = "  {\"lbl\": \"%s\", \"ts\": \"%s\", \"val\": \"%s\" }";
//static const char *FORMAT_NUMERIC_SANS_HORODATE = "  {\"lbl\": \"%s\", \"val\": %d }";
//static const char *FORMAT_NUMERIC_AVEC_HORODATE = "  {\"lbl\": \"%s\", \"ts\": \"%s\", \"val\": %d }";
static const char *FORMAT_ISO8601 = "%Y-%m-%dT%H:%M:%S%z";

static const char *FORMAT_STRING_SANS_HORODATE = "  \"%s\" : { \"val\":\"%s\" }";
static const char *FORMAT_NUMERIC_SANS_HORODATE = "  \"%s\" : { \"val\":%d }";


static size_t printf_ds( char *buf, size_t size, const tic_dataset_t *ds )
{
    if( ds->flags & TIC_DS_NUMERIQUE )
    {
        // formatte la valeur numerique avec ou sans horodate
        uint32_t val = strtol( ds->valeur, NULL, 10 );
        return snprintf( buf, size, FORMAT_NUMERIC_SANS_HORODATE, ds->etiquette, val);
    }
    else
    {
        return snprintf( buf, size, FORMAT_STRING_SANS_HORODATE, ds->etiquette, ds->valeur);
    }
}


static size_t get_time_iso8601( char *buf, size_t size )
{
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r( &now, &timeinfo );
    return strftime( buf, size, FORMAT_ISO8601, &timeinfo );
}


static tic_dataset_t *filtre_datasets( tic_dataset_t *ds )
{
    tic_dataset_t *head = NULL; 
    tic_dataset_t *tail = NULL;   // pointeur sur la queue pour conserver l'ordre
    while( ds != NULL )
    {
        tic_dataset_t *tmp_next = ds->next;
        ds->next = NULL;

        if( ds->flags & TIC_DS_PUBLISHED )
        {
            if( !head )
                head = ds;

            if( !tail )
                tail = ds;

            tail->next = ds;
            tail = ds;
        }
        else
        {
            free(ds);
        }
        ds = tmp_next;
    }
    return head;
}

// compare deux trames - les datasets doivent être triés !
int compare_datasets( const tic_dataset_t *ds1, const tic_dataset_t *ds2 )
{
    while( (ds1!=NULL) && (ds2!= NULL) )
    {
        int cmp_label = strncmp( ds1->etiquette, ds2->etiquette, sizeof( ds1->etiquette) );
        if( cmp_label != 0 )
            return cmp_label;

        int cmp_value = strncmp( ds1->valeur, ds2->valeur, sizeof( ds1->valeur) );
        if( cmp_value != 0 )
            return cmp_value;

        ds1 = ds1->next;
        ds2 = ds2->next;
    }
    // nombre de datasets différents
    if( (ds1!=NULL) || (ds2!=NULL) )
        return -1;

    return 0;
}


static mqtt_error_t datasets_to_topic (char *buf, size_t size, const tic_dataset_t *ds )
{
    while( ds != NULL )
    {
        if( strcmp( ds->etiquette, LABEL_ID_DEVICE ) == 0 )
        {
            snprintf( buf, size, MQTT_TOPIC_FORMAT, ds->valeur );
            ESP_LOGI( TAG , "Topic=%s", buf );
            return MQTT_OK;
        }
        ds = ds->next;
    }
    ESP_LOGE( TAG, "ADCO (identifiant compteur) absent");
    return MQTT_ERR_MISSING_DATA;
}


static mqtt_error_t datasets_to_json( char *buf, size_t size, const tic_dataset_t *ds )
{
    size_t pos = 0;

    char time_buf[30];
    get_time_iso8601( time_buf, sizeof(time_buf) );

    pos += snprintf( &(buf[pos]), size-pos, "{  \"horodate\" : \"%s\",\n  \"tic\" : {\n", time_buf );

    while( ds!=NULL && (size-pos) > 0 )
    {
        // ignore les etiquettes non exportées 
        if( (ds->flags & TIC_DS_PUBLISHED) == 0  )
        {
            ESP_LOGE( TAG, "Probleme avec le filtrage en amont de datasets_to_json()");
            ds = ds->next;
            continue;
        }

        // formatte la donnée en JSON
        pos += printf_ds( &(buf[pos]), size-pos, ds );

        // separateurs
        if( pos >= (size-2) )     // -2 pour la virgule et le \n 
        {
            ESP_LOGE( TAG, "JSON buffer overflow" );
            return MQTT_ERR_OVERFLOW;
        }
        if( ds->next != NULL) 
        {
            buf[pos++] = ',';
        }
        buf[pos++] = '\n';

        ds = ds->next;
    }

    // termine le tableau et l'objet JSON racine
    pos += snprintf( &(buf[pos]), size-pos, "} }\n" );
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
    if( esp_mqtt_client_start(params->esp_client) != ESP_OK ) {
        ESP_LOGE( TAG, "esp_mqtt_client_start() erreur %d", err);
    }

    char *json_buffer = malloc(MQTT_JSON_BUFFER_SIZE);
    char *topic_buffer = malloc(MQTT_TOPIC_BUFFER_SIZE);

    TickType_t max_ticks = MQTT_TIC_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS; 
    tic_dataset_t *ds_last = NULL; 
    tic_dataset_t *ds_new = NULL;
    for(;;)
    {
        tic_dataset_free( ds_new );    ///libère la memoire allouée par tic_decode
        ds_new = NULL;

        BaseType_t ds_received = xQueueReceive( params->from_decoder, &ds_new, max_ticks );
        if( ds_received != pdTRUE )
        {
            ESP_LOGI( TAG, "Aucune trame téléinfo reçue depuis %d secondes (%p)", MQTT_TIC_TIMEOUT_SEC, ds_new );
            continue;
        }

        if( datasets_to_topic (topic_buffer, MQTT_TOPIC_BUFFER_SIZE, ds_new ) != MQTT_OK )
        {
            continue;
        }

        ds_new = filtre_datasets( ds_new );

        if( compare_datasets( ds_last, ds_new ) != 0 )
        {
            // les donnnees ont changé, on les garde et on les publie
            tic_dataset_free( ds_last );
            ds_last = ds_new;
            ds_new = NULL;
        }
        else
        {
            //ESP_LOGD( TAG, "Données non modifiées donc non publiées");
            continue;
        }

        if( datasets_to_json( json_buffer, MQTT_JSON_BUFFER_SIZE, ds_last ) != MQTT_OK )
        {
            continue;
        }

        int msg_id = esp_mqtt_client_publish( params->esp_client, topic_buffer, json_buffer, 0, 0, 0);
        if( msg_id < 0 )
        {
            //ESP_LOGE( TAG, "Echec de la publication mqtt");
        }
    }

    ESP_LOGE( TAG, "fatal: mqtt_task exited" );
    if( json_buffer != NULL ) {
        free(json_buffer);
        json_buffer = NULL;
    }
    if( topic_buffer != NULL ) {
        free(topic_buffer);
        topic_buffer = NULL;
    }
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


BaseType_t mqtt_task_start( QueueHandle_t from_decoder )
{
    //esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set( TAG, ESP_LOG_DEBUG );
    //esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    //esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    //esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    //esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    //esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

   esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = BROKER_HOST,
        .broker.address.port = BROKER_PORT,
        .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
        .broker.verification.psk_hint_key = &psk_hint_key,
    };

    log_mqtt_cfg( mqtt_cfg );
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    if( client == NULL )
    {
        ESP_LOGE( TAG, "esp_mqtt_client_init() failed");
        return pdFALSE;
    }

    esp_err_t err;
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    err = esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL );
    if( err != ESP_OK ) {
        ESP_LOGE( TAG, "esp_mqtt_client_register_event() erreur %d", err);
        return pdFALSE;
    }

    // setup inter-task communication stuff
    mqtt_task_param_t *task_params = malloc( sizeof( mqtt_task_param_t ) );
    task_params->from_decoder = from_decoder;
    task_params->esp_client = client;

    // create mqtt client task
    BaseType_t task_created = xTaskCreate( mqtt_task, "mqtt_task", 4096, task_params, 12, NULL);
    if( task_created != pdPASS )
    {
        ESP_LOGE( TAG, "xTaskCreate() failed");
    }
    return task_created;
}
