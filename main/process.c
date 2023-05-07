

#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/event_groups.h"

#include "esp_log.h"

#include "tic_types.h"
#include "tic_config.h"
#include "dataset.h"
#include "event_loop.h"
#include "mqtt.h"        // pour mqtt_msg_alloc() mqtt_msg_free()
#include "process.h"
#include "puissance.h"

static const char *TAG = "process.c";

#define TIC_LAST_POINTS_CNT  10

static QueueHandle_t s_to_process = NULL;


static const char *FORMAT_ISO8601 = "%Y-%m-%dT%H:%M:%S%z";
static const char *FORMAT_NUMERIC_SANS_HORODATE = "  \"%s\":{\"val\":%"PRIi32"}";
static const char *FORMAT_NUMERIC_AVEC_HORODATE = "  \"%s\":{\"horodate\":\"%s\", \"val\":%"PRIi32"}";
static const char *FORMAT_STRING_SANS_HORODATE = "  \"%s\":{\"val\":\"%s\"}";
static const char *FORMAT_STRING_AVEC_HORODATE = "  \"%s\":{\"horodate\":\"%s\", \"val\":\"%s\"}";




static tic_error_t set_topic (char *buf, size_t size, const tic_data_t *data )
{
    assert(data->id_compteur);
    snprintf ( buf, size, MQTT_TOPIC_FORMAT, data->id_compteur );  // ignore errors
    return TIC_OK;
}


static size_t printf_ds( char *buf, size_t size, const dataset_t *ds )
{
    // garde seulement les flags de format
    tic_dataset_flags_t flags =  (ds->flags) & (TIC_DS_NUMERIQUE|TIC_DS_HAS_TIMESTAMP);
    int32_t val_int=-1;

    // convertit en int pour un formattage printf correct 
    if( flags & TIC_DS_NUMERIQUE )
    {
        // TODO : check erreurs de conversion
        val_int = strtol( ds->valeur, NULL, 10 );
    }

    size_t nb_wr=0;
    switch( flags)
    {
        case 0:
            nb_wr = snprintf( buf, size, FORMAT_STRING_SANS_HORODATE, ds->etiquette, ds->valeur);
            break;
        case TIC_DS_HAS_TIMESTAMP:
            nb_wr = snprintf( buf, size, FORMAT_STRING_AVEC_HORODATE, ds->etiquette, ds->horodate, ds->valeur);
            break;
        case TIC_DS_NUMERIQUE:
            nb_wr = snprintf( buf, size, FORMAT_NUMERIC_SANS_HORODATE, ds->etiquette, val_int);
            break;
        case TIC_DS_NUMERIQUE|TIC_DS_HAS_TIMESTAMP:
            nb_wr = snprintf( buf, size, FORMAT_NUMERIC_AVEC_HORODATE, ds->etiquette, ds->horodate, val_int);
            break;
    }

   return nb_wr;
}


static size_t get_time_iso8601( char *buf, size_t size )
{
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r( &now, &timeinfo );
    return strftime( buf, size, FORMAT_ISO8601, &timeinfo );
}

/*
// compare deux trames - les datasets doivent être triés !
int compare_datasets( const dataset_t *ds1, const dataset_t *ds2 )
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
*/

static tic_error_t datasets_to_json( char *buf, size_t size, const dataset_t *ds )
{
    size_t pos = 0;
    char time_buf[30];
    get_time_iso8601( time_buf, sizeof(time_buf) );

    pos += snprintf( &(buf[pos]), size-pos, "{\n\"esp_time\":\"%s\",\n\"esp_free_mem\":%"PRIu32",\n\"tic\" : {\n", time_buf, esp_get_free_heap_size() );

    while( ds!=NULL && (size-pos) > 0 )
    {
        // ignore les etiquettes non exportées 
        if( (ds->flags & TIC_DS_PUBLISHED) == 0  )
        {
            ds = ds->next;
            continue;
        }

        // formatte la donnée en JSON
        pos += printf_ds( &(buf[pos]), size-pos, ds );

        // separateurs
        if( pos >= (size-2) )     // -2 pour la virgule et le \n 
        {
            ESP_LOGE( TAG, "JSON buffer overflow" );
            return TIC_ERR_OVERFLOW;
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
        return TIC_ERR_OVERFLOW;
    }
    return TIC_OK;
}


static tic_error_t set_payload( char *buf, size_t size, dataset_t *ds )
{
    dataset_t * p_actives = puissance_get_all();
    ds = dataset_append( ds, p_actives );
    return datasets_to_json( buf, size, ds );
}


static tic_error_t build_mqtt_msg( mqtt_msg_t *msg, dataset_t *ds, const tic_data_t *data )
{
    tic_error_t err;
    err = set_topic( msg->topic, MQTT_TOPIC_BUFFER_SIZE, data );
    if( err != TIC_OK )
    {
        ESP_LOGD( TAG, "Erreur lors de la création du topic MQTT");
        return err;
    }

    err = set_payload( msg->payload, MQTT_PAYLOAD_BUFFER_SIZE, ds );
    if( err != TIC_OK )
    {
        ESP_LOGD( TAG, "Erreur lors de la création du payload MQTT");
        return err;
    }
    return TIC_OK;

}


static tic_error_t traite_donnees( const tic_data_t *data )
{
    // mise à jour afficheur oled, etc
    send_event_puissance (data->puissance_app); // ignore errors

    // envoie la trame au module de calcul de puissance active
    puissance_incoming_data( data );     // ignore erreurs

    return TIC_OK;
}


static void process_task( void *pvParams )
{
    ESP_LOGI( TAG, "process_task()");

    mqtt_msg_t *msg = NULL;
    dataset_t *ds = NULL;
    tic_error_t err;
    tic_data_t data;

    for(;;)
    {
        //ESP_LOGD( TAG, "START process_task loop ds=%p msg=%p ...", ds, msg );
        dataset_free( ds );    //libère les datasets reçus de decode_task
        ds = NULL;

        mqtt_msg_free( msg );        // libere les msg non-envoyés à mqtt_task
        msg=NULL;

        BaseType_t ds_received = xQueueReceive( s_to_process, &ds, portMAX_DELAY );
        if( ds_received != pdTRUE )
        {
            ESP_LOGD( TAG, "Aucune trame téléinfo reçue depuis %d secondes", TIC_PROCESS_TIMEOUT );
            continue;
        }

        //uint32_t nb=dataset_count(ds);
        //ESP_LOGD( TAG, "%"PRIu32" datasets reçus ds=%p &ds=%p", nb, ds, &ds);

        // extrait les données utiles et effectue les traitements 
        err = dataset_parse( ds, &data );
        if( err == TIC_OK )
        {
            traite_donnees( &data ); // ignore erreurs et continue dans tous les cas
        }

        msg = mqtt_msg_alloc();
        if( msg == NULL)
        {
            continue;   // erreur logguee dans mqtt_alloc_msg()
        }

        err = build_mqtt_msg (msg, ds, &data);
        if(err != TIC_OK)
        {
            ESP_LOGE (TAG, "build_mqtt_msg() erreur %d", err);
            continue;    // erreur logguee dans build_mqtt_msg()
        }

        // envoie le message à mqtt_task
        if( mqtt_receive_msg(msg) == TIC_OK )
        {
            msg = NULL;   // sera liberé par mqtt_task;
        }
    }
    ESP_LOGE( TAG, "fatal: process_task exited" );
    vTaskDelete(NULL);
}


tic_error_t process_receive_datasets( dataset_t *ds )
{
    if( s_to_process == NULL )
    {
        ESP_LOGE( TAG, "queue s_to_process pas initialisée" );
        return TIC_ERR;
    }

    BaseType_t send_ok = xQueueSend( s_to_process, &ds, 10 );
    if( send_ok != pdTRUE )
    {
        ESP_LOGE( TAG, "Queue pleine : impossible de recevoir la trame TIC decodee" );
        return TIC_ERR_QUEUEFULL;
    }
    //uint32_t nb = dataset_count(ds);
    //ESP_LOGD( TAG, "%"PRIu32" datasets mis dans la queue %p", nb, ds);
    return TIC_OK;
}


tic_error_t process_task_start( QueueHandle_t to_decoder, QueueHandle_t to_mqtt )
{
    puissance_init();

    // reçoit les trames décodées par decode_task
    s_to_process = xQueueCreate( 5, sizeof( dataset_t * ) );
    if( s_to_process==NULL )
    {
        ESP_LOGE( TAG, "xCreateQueue() failed" );
        return TIC_ERR_APP_INIT;
    }

    // create mqtt client task
    BaseType_t task_created = xTaskCreate( process_task, "process_task", 4096, NULL, 12, NULL);
    if( task_created != pdPASS )
    {
        ESP_LOGE( TAG, "xTaskCreate() failed");
        return TIC_ERR_APP_INIT;
    }
    return TIC_OK;
}
