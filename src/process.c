

#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/event_groups.h"

#include "esp_log.h"

#include "errors.h"
#include "decode.h"
#include "flags.h"
#include "status.h"
#include "mqtt.h"
#include "process.h"


static const char *TAG = "process.c";

#define TIC_LAST_POINTS_CNT  10

//static const char *LABEL_ADCO = "ADCO";
static const char *LABEL_DEVICE_ID = "ADSC";
static const char *LABEL_ENERGIE_ACTIVE_TOTALE = "EAST";
static const char *LABEL_HORODATE = "DATE";

static QueueHandle_t s_to_process = NULL;
/*
typedef struct process_task_param_s {
    QueueHandle_t to_process;
    QueueHandle_t to_mqtt;
} process_task_param_t;
*/


typedef struct point_east_s {
    time_t ts;      // timestamp du point
    int32_t east;                       // index d'energie active soutiree totale du compteur
} east_point_t;


// conserve les derniers points reçus
static east_point_t s_east_rb[TIC_LAST_POINTS_CNT];    // ring buffer
static int8_t s_east_current;


static void init_east_rb()
{
    memset( &s_east_rb, 0, TIC_LAST_POINTS_CNT*sizeof(east_point_t));
    s_east_current = 0;
}

// recupere un point dans le ring buffer
// i=0 : le plus récent   
//i=cnt : le plus ancien
const east_point_t* get_east_point( int8_t i )
{
    int8_t idx = ( i + s_east_current ) % TIC_LAST_POINTS_CNT;
    return &(s_east_rb[idx]);
}

// ajoute un nouveau point dans le ring buffer
static void add_east_point( const east_point_t * pt )
{
    int8_t pos = (s_east_current - 1) % TIC_LAST_POINTS_CNT;   // les points sont stockés "à l'envers"
    ESP_LOGD( TAG, "add_east_point() s_east_current=%"PRIi8" pos=%"PRIi8" ts=%"PRIi64" east=%"PRIi32, s_east_current, pos, pt->ts, pt->east );
    s_east_rb[pos].east = pt->east;
    s_east_rb[pos].ts = pt->ts;
}


int calcule_p_active ( uint8_t n )
{

    if( n<1 || n>TIC_LAST_POINTS_CNT)
    {
        ESP_LOGW( TAG, "calcule_p_active(%d) avec n invalide", n);
        return -1;
    }

    const east_point_t *p0 = get_east_point( 0 );
    const east_point_t *pN = get_east_point( n );

    if( pN->east==0 || pN->ts==0 || p0->east==0 || p0->ts==0 )
    {
        ESP_LOGW( TAG, "calcule_p_active(%d) impossible, pas encore assez de points reçus", n);
        return -1;
    }

    int32_t energie = pN->east - p0->east;
    time_t duree = pN->ts - p0->ts;

    if( duree == 0 )
    {
        ESP_LOGW( TAG, "calcule_p_active(%d) impossible : les deux index ont la même horodate)", n);
        return -1;
    }

    return (3600*energie)/duree;    // energie est en Watt.heure, on veut des Watt.seconde
}


#define TSFRAGMENT_BUFSIZE 8
static tic_error_t tsfragment_to_int( const char *start, int read_len, int *val, int offset )
{
    char buf[TSFRAGMENT_BUFSIZE];
    if( read_len >= TSFRAGMENT_BUFSIZE-1 )
    {
        ESP_LOGE( TAG, "overflow in tsfragment_to_int()" );
        return TIC_ERR_OVERFLOW;
    }

    strncpy( buf, start, read_len );
    buf[read_len]= '\0';

    //ESP_LOGD( TAG, "strtol(%s)", buf );
    char *end;
    *val = strtol( buf, &end, 10);
    if( end-buf != read_len)
    {
        ESP_LOGE( TAG, "erreur strtol() sur %s", buf);
        return TIC_ERR_BAD_DATA;
    }
    *val += offset;
//    ESP_LOGD( TAG, "val=%d", *val );
    return TIC_OK;
}


static tic_error_t horodate_to_time_t( const char *horodate, time_t *unix_time)
{
    ESP_LOGD( TAG, "horodate_to_time_t(%s)", horodate  );
/*
    struct tm tm1 = { .tm_year=123, 
                     .tm_mon=4,
                     .tm_mday=8,
                     .tm_hour=12,
                     .tm_min=2,
                     .tm_sec=8, 
                     .tm_isdst=-1 };
    time_t t = mktime( &tm1 );

    ESP_LOGD( TAG, "time_t t = %"PRIi64, t);

    struct tm timeinfo;
    localtime_r( &t, &timeinfo );
    strftime( timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &timeinfo );
    ESP_LOGI( TAG, "local time %s", timebuf );
*/

    struct tm tm;

    // utilise l'indication d'heure d'été reçue
    switch( horodate[0] )      
    {
        case 'E':
            tm.tm_isdst = 1;     // E = heure d'été
            break;
        case 'H':
            tm.tm_isdst = 0;     // H = heure d'hiver
            break;
        default:
            tm.tm_isdst = -1;
    }

    tic_error_t err= TIC_OK;

    //   tm_year commence en 1900, la teleinfo commence en 2000
    err = tsfragment_to_int( &(horodate[1]), 2, &tm.tm_year, 100 );
    if( err != TIC_OK ) { return err; }

    //   tm_mon 0->11   teleinfo = 1->12
    err = tsfragment_to_int( &(horodate[3]), 2, &(tm.tm_mon), -1 );
    if( err != TIC_OK ) { return err; }

    err = tsfragment_to_int( &(horodate[5]), 2, &(tm.tm_mday), 0 );
    if( err != TIC_OK ) { return err; }

    err = tsfragment_to_int( &(horodate[7]), 2, &(tm.tm_hour), 0 );
    if( err != TIC_OK ) { return err; }

    err = tsfragment_to_int( &(horodate[9]), 2, &(tm.tm_min), 0 );
    if( err != TIC_OK ) { return err; }

    err = tsfragment_to_int( &(horodate[11]), 2, &(tm.tm_sec), 0 );
    if( err != TIC_OK ) { return err; }

    // pour le debug
    char timebuf[60];
    strftime( timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm );
    ESP_LOGD( TAG, "horodate decodéee %s", timebuf );

    // renvoie le timestamp unix sur le pointeur fourni
    if( unix_time != NULL )
    {
        *unix_time = mktime( &tm );
    }
    return TIC_OK;
}


static tic_error_t east_to_point( east_point_t *pt, const char *horodate, const char *east_index) 
{
    ESP_LOGD( TAG, "east_to_point() : horodate=%s index=%s", horodate, east_index );
    
    // traite l'horodate
    time_t ts;
    tic_error_t err = horodate_to_time_t( horodate, &ts );
    if( err != TIC_OK )
    {
        return err;
    }

    // traite l'index
    char *end;
    int val = strtol( east_index, &end, 10);
    if( *end != '\0')
    {
        ESP_LOGE( TAG, "erreur strtol() sur %s", east_index );
        return TIC_ERR_BAD_DATA;
    }

    pt->ts = ts;
    pt->east = val;
    // ESP_LOGD( TAG, "east_to_point() : ts=%"PRIi64" east=%"PRIi32, pt->ts, pt->east );
    return TIC_OK;
}


static const tic_dataset_t * cherche_ds( const tic_dataset_t *ds, const char *etiquette )
{
    while( ds != NULL )
    {
        if( strncmp( ds->etiquette, etiquette, TIC_SIZE_ETIQUETTE ) == 0 )
        {
            return ds;
        }
        ds = ds->next;
    }
    return NULL;
}


static tic_error_t historise_east( const tic_dataset_t *ds )
{
    east_point_t *pt = NULL;

    const tic_dataset_t *ds_horodate = cherche_ds( ds, LABEL_HORODATE );
    if( ds_horodate == NULL )
    {
        ESP_LOGE( TAG, "Donnee DATE manquante");
        return TIC_ERR_MISSING_DATA;
    }
 
    const tic_dataset_t *ds_east_index = cherche_ds( ds, LABEL_ENERGIE_ACTIVE_TOTALE );
    if( ds_east_index == NULL )
    {
        ESP_LOGE( TAG, "Donnee EAST manquante");
        return TIC_ERR_MISSING_DATA;
    }

    tic_error_t err = east_to_point( pt, ds_horodate->horodate, ds_east_index->valeur );
    if( err != TIC_OK )
    {
        return err;
    }

    add_east_point( pt );  // ne renvoie jamais d'erreur
    return TIC_OK;
}


static tic_error_t datasets_to_topic (char *buf, size_t size, const tic_dataset_t *ds )
{
    while( ds != NULL )
    {
        if( strcmp( ds->etiquette, LABEL_DEVICE_ID ) == 0 )
        {
            snprintf( buf, size, MQTT_TOPIC_FORMAT, ds->valeur );
            ESP_LOGI( TAG , "Topic=%s", buf );
            return TIC_OK;
        }
        ds = ds->next;
    }
    ESP_LOGE( TAG, "Identifiant compteur %s absent", LABEL_DEVICE_ID );
    return TIC_ERR_MISSING_DATA;
}


// envoie, ou non, des donnes pour mettre à jour l'afficheur
//static tic_error_t affiche_dataset( tic_decoder_t *td, const tic_dataset_t *ds )
static tic_error_t affiche_dataset( const tic_dataset_t *ds )
{
    
    //if( strcmp( ds->etiquette, "PAPP" ) == 0 )
    if( strcmp( ds->etiquette, "SINST" ) == 0 )
    {
       // oled_update( td->to_oled, DISPLAY_PAPP, ds->valeur );
        uint32_t papp = strtol( ds->valeur, NULL, 10 );
        status_papp_update( papp );
    }
    //ESP_LOGD ( TAG, "%s %s", ds->etiquette, ds->valeur);
    return TIC_OK;
}


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


static tic_error_t datasets_to_json( char *buf, size_t size, const tic_dataset_t *ds )
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

static tic_error_t build_mqtt_msg( mqtt_msg_t *msg, const tic_dataset_t *ds  )
{

    affiche_dataset( ds );
    datasets_to_topic( NULL, 0, ds );
    filtre_datasets( NULL );
    datasets_to_json( NULL, 0, ds );
    /*
    char *json_buffer = malloc(TIC_PROCESS_JSON_BUFFER_SIZE);
    char *topic_buffer = malloc(TIC_PROCESS_TOPIC_BUFFER_SIZE);

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

*/
    return TIC_ERR;
}


static void process_task( void *pvParams )
{
    ESP_LOGI( TAG, "process_task()");

    //process_task_param_t *params = (process_task_param_t *)pvParams;

    TickType_t max_ticks = TIC_PROCESS_TIMEOUT * 1000 / portTICK_PERIOD_MS; 
//    tic_dataset_t *ds_last = NULL; 
    tic_dataset_t *ds = NULL;

    mqtt_msg_t *msg = NULL;

    for(;;)
    {
        tic_dataset_free( ds );    //libère les datasets reçus de decode_task
        ds = NULL;

        mqtt_msg_free( msg );        // libere les msg non-envoyés à mqtt_task
        msg=NULL;


        BaseType_t ds_received = xQueueReceive( s_to_process, &ds, max_ticks );
        if( ds_received != pdTRUE )
        {
            ESP_LOGI( TAG, "Aucune trame téléinfo reçue depuis %d secondes", TIC_PROCESS_TIMEOUT );
            continue;
        }

        historise_east( ds );

        mqtt_msg_t *msg = mqtt_msg_alloc();
        if( msg == NULL)
        {
            continue;   // erreur logguee dans mqtt_alloc_msg()
        }

        if( build_mqtt_msg( msg, ds ) != TIC_OK)
        {
            continue;    // erreur logguee dans build_mqtt_msg()
        }

        if( mqtt_receive_msg(msg) == TIC_OK )
        {
            msg = NULL;   // sera liberé par mqtt_task;
        }
    }
    ESP_LOGE( TAG, "fatal: process_task exited" );
    vTaskDelete(NULL);
}


tic_error_t process_receive_datasets( tic_dataset_t *ds )
{
    BaseType_t send_ok = xQueueSend( s_to_process, ds, 10 );
    if( send_ok != pdTRUE )
    {
        ESP_LOGE( TAG, "Queue pleine : impossible de recevoir la trame TIC decodee" );
        return TIC_ERR_QUEUEFULL;
    }
    return TIC_OK;
}


BaseType_t process_task_start( QueueHandle_t to_decoder, QueueHandle_t to_mqtt )
{
    esp_log_level_set( TAG, ESP_LOG_DEBUG );

    init_east_rb();
/*
    tic_dataset_t ds_test = {
        .etiquette = "EAST",
        .horodate = "E220408232221",
        .valeur = "001254",
    };

    
    point_east_t pt = {0};

    tic_error_t err = east_to_point( &pt, ds_test.horodate, ds_test.valeur );
    if( err != TIC_OK )
    {
        ESP_LOGD( TAG, "east_to_point erreur %d", err);
        return pdFALSE;
    }
*/

    // reçoit les trames décodées par decode_task
    s_to_process = xQueueCreate( 5, sizeof( tic_dataset_t * ) );
    if( s_to_process==NULL )
    {
        ESP_LOGE( TAG, "xCreateQueue() failed" );
        return pdFALSE;
    }

    // create mqtt client task
    BaseType_t task_created = xTaskCreate( process_task, "process_task", 4096, NULL, 12, NULL);
    if( task_created != pdPASS )
    {
        ESP_LOGE( TAG, "xTaskCreate() failed");
    }
    return task_created;
}
