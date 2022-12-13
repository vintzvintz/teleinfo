#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/stream_buffer.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "tic_decode.h"
#include "status.h"
#include "oled.h"
#include "ticled.h"

static const char *TAG = "tic_decode";

#define CHAR_STX  0x02    //   start of text - début d'une trame
#define CHAR_ETX  0x03    //   end of text - fin d'une trame
#define CHAR_CR   '\r'
#define CHAR_LF   '\n'

#define TIC_MODE_HISTORIQUE

// separateur dans un groupe de données
#ifdef TIC_MODE_HISTORIQUE
    #define TIC_SEPARATOR  ' '    /* SPACE */
#elif TIC_MODE_STANDARD
    #define TIC_SEPARATOR  0x09    /* TAB */
#endif


typedef struct tic_taskdecode_params_s {
    StreamBufferHandle_t from_uart;
    QueueHandle_t to_mqtt;
    EventGroupHandle_t to_ticled;
    QueueHandle_t to_oled;
} tic_taskdecode_params_t;

/***
 * tic_frame_s contient les données d'une trame en cours de réception 
 */
typedef struct tic_decoder_s {
    
    // etat de la frame complète
    uint8_t stx_received;
    tic_dataset_t *datasets;         // linked list des datasets complets reçus

    // buffers pour le dataset en cours de reception
    tic_char_t buf0[TIC_SIZE_BUF0];   // etiquette
    tic_char_t buf1[TIC_SIZE_BUF1];   // horodate ou valeur
    tic_char_t buf2[TIC_SIZE_BUF2];   // valeur ou checksum reçu
    tic_char_t buf3[TIC_SIZE_BUF3];   // checksum reçu ou NULL

    // selecteur du buffer courant
    tic_char_t *cur_buf;
    size_t cur_buf_size;

    // queue pour envoyer les trames completes au client mqtt
    QueueHandle_t to_mqtt;

    // event_groups pour faire clignoter la Led
    EventGroupHandle_t to_ticled;

    // queue pour envoyer les trames completes au client mqtt
    QueueHandle_t to_oled;


} tic_decoder_t;


uint32_t tic_dataset_count( tic_dataset_t *dataset )
{
    uint32_t nb = 0;
    while ( dataset != NULL )
    {
        nb++;
        dataset = dataset->next;
    }
    return nb;
}

uint32_t tic_dataset_size( tic_dataset_t *dataset )
{
    uint32_t size = 0;
    while ( dataset != NULL )
    {
        size += strlen( dataset->etiquette) + 1;    // 1 separator
        size += strlen( dataset->horodate) + 1;     // 1 separator
        size += strlen( dataset->valeur) + 1;       // '\n' ou '\0'
        dataset = dataset->next;
    }
    return size;
}

tic_error_t tic_dataset_print( tic_dataset_t *dataset )
{
    // ESP_LOGD( TAG, "print_datasets()");
    while( dataset != NULL )
    {
        if( dataset->horodate[0] == '\0')
        {
            ESP_LOGI( TAG, "%s\t%s", dataset->etiquette, dataset->valeur );
        }
        else
        {
            ESP_LOGI( TAG, "%s\t%s\t%s", dataset->etiquette, dataset->horodate, dataset->valeur );
        }
        dataset = dataset->next;
    }
    return TIC_OK;
}


void tic_dataset_free( tic_dataset_t *ds )
{
    while ( ds != NULL )
    {
        tic_dataset_t *tmp = ds;
        ds = ds->next;
        free( tmp );
    }
}

/*
static int my_strcmp( const char * s1, const char *s2 )
{
    int ret = strcmp( s1, s2 );
    ESP_LOGD( TAG, "strcmp( %s, %s ) = %d",s1,s2,ret);
    return ret;
}


static void debug_list( const char *nom, tic_dataset_t *ds )
{
    char buf[256];
    int pos = 0;
    pos += snprintf( buf, sizeof(buf), "%s = ", nom );
    while( ds != NULL )
    {
        pos += snprintf( &buf[pos], sizeof(buf), "%s->", ds->etiquette );
        ds = ds->next;
    }
    ESP_LOGD( TAG, "%s", buf);
}
*/

static tic_dataset_t* insert_sort( tic_dataset_t *sorted, tic_dataset_t *ds)
{
    assert( ds != NULL );           // l'insertion de NULL est invalide
    assert( ds->next == NULL );     // ds doit être un element isolé, le ptr sur le suivant doit rester chez l'appelant

    tic_dataset_t *item = sorted;

    while( item != NULL )
    {
        //ESP_LOGD( TAG, "essaye d'inserer %s sur %s", ds->etiquette, item->etiquette );

        if( strcmp( ds->etiquette, item->etiquette ) < 0 )
        {
            // ds est plus petit que l'élément courant
            assert( item == sorted ); // impossible sauf pour le premier element de la liste triée
            //ESP_LOGD( TAG, "insere %s avant %s (en premier)", ds->etiquette, item->etiquette );
            ds->next = item;
            sorted = ds;
            break;
        }
        // ds est plus grand que l'élément courant
        // on l'insère dans la liste si 
        //    s'il est plus petit que l'élément suivant,
        //    ou s'il n'y a pas d'élement suivant 
        if ( (item->next == NULL) || (strcmp( ds->etiquette, item->next->etiquette ) <= 0 ) )
        {
            /*
            if( item->next == NULL )
            {
                ESP_LOGD( TAG, "insere %s après %s (en dernier)", ds->etiquette, item->etiquette );
            }
            else
            {
                ESP_LOGD( TAG, "insere %s après %s et avant %s", ds->etiquette, item->etiquette, item->next->etiquette );
            }
            */
            ds->next = item->next;
            item->next = ds;
            break;
        }
        item = item->next;
    }

    if( sorted == NULL )
    {
        //ESP_LOGD( TAG, "la liste triée est vide, renvoie %s", ds->etiquette );
        sorted = ds;
    }

    return sorted;

}


tic_dataset_t * tic_dataset_sort(tic_dataset_t *ds)
{
    tic_dataset_t * sorted = NULL;
    tic_dataset_t * ds_next = NULL;


    while( ds != NULL )
    {
        //ESP_LOGD( TAG, "tic_dataset_sort(%s)", ds->etiquette );
        //debug_list("ds", ds);

        // copie le ptr vers l'item suivant car ds->next va être modifié lors de son insertion dans 'sorted'
        ds_next = ds->next;
        ds->next=NULL;

        sorted = insert_sort( sorted, ds );
        //debug_list( "sorted", sorted);
        ds = ds_next;
    }
    return sorted;
}



static void reset_decoder( tic_decoder_t *td )
{
    //ESP_LOGD( TAG, "reset_decoder()");

    // membres conservés
    QueueHandle_t to_mqtt = td->to_mqtt;
    EventGroupHandle_t to_ticled = td->to_ticled;
    QueueHandle_t to_oled = td->to_oled;

    // desalloue les datasets
    tic_dataset_free( td->datasets );

    // met à 0 toutes les variables et buffers 
    memset( td, 0, sizeof(tic_decoder_t) );

    // restaure les membres conservés
    td->to_mqtt = to_mqtt;
    td->to_ticled = to_ticled;
    td->to_oled = to_oled;
}


static tic_error_t dataset_start( tic_decoder_t *td )
{
    //ESP_LOGD( TAG, "dataset_start()");

    // limite le nombre de datasets dans une trame
    if( tic_dataset_count( td->datasets ) >= TIC_MAX_DATASETS )
    {
        ESP_LOGE(TAG, "Trop de datasets dans une trame (TIC_MAX_DATASETS=%d)", TIC_MAX_DATASETS );
        return TIC_ERR_OVERFLOW;
    }

    // clear buffers 
    memset( td->buf0, 0, TIC_SIZE_BUF0 );
    memset( td->buf1, 0, TIC_SIZE_BUF1 );
    memset( td->buf2, 0, TIC_SIZE_BUF2 );
    memset( td->buf3, 0, TIC_SIZE_BUF3 );
 
    // prepare la réception sur le 1r buffer
    td->cur_buf = td->buf0;
    td->cur_buf_size = TIC_SIZE_BUF0;
    return TIC_OK;
}


// ajoute chaque caractere de la chaine buf à la somme s1
// TODO : ajouter un parametre avec la taille max du buffer
static void addbuf( uint32_t *s1, const tic_char_t *buf )
{
    const tic_char_t *p = buf;
    while( *p != '\0' )
    {
        *s1 += *p;
        p++;
    }
}


static tic_error_t affiche_dataset( tic_decoder_t *td, const tic_dataset_t *ds )
{
    if( strcmp( ds->etiquette, "PAPP" ) == 0 )
    {
       oled_update( td->to_oled, DISPLAY_PAPP, ds->valeur );
    }
    //ESP_LOGD ( TAG, "%s %s", ds->etiquette, ds->valeur);
    return TIC_OK;
}


static tic_error_t dataset_end( tic_decoder_t *td ) {
    //ESP_LOGD( TAG, "dataset_end()");

    tic_char_t *buf_etiquette, *buf_horodate, *buf_valeur, *buf_checksum;

    if( td->buf3[0] != '\0' )          // dataset avec horodate, 4 elements
    {
        buf_etiquette = td->buf0;
        buf_horodate  = td->buf1;
        buf_valeur    = td->buf2;
        buf_checksum  = td->buf3;
    } 
    else                          // dataset sans horodate, 3 elements
    {
        buf_etiquette = td->buf0;
        buf_horodate  = NULL;
        buf_valeur    = td->buf1;
        buf_checksum  = td->buf2;
    }

    // calcule le checksum
    uint32_t s1 = 0;
    addbuf( &s1, buf_etiquette );
    s1 += TIC_SEPARATOR;
    if( buf_horodate != NULL )
    {
        addbuf( &s1, buf_horodate );
        s1 += TIC_SEPARATOR;
    }
    addbuf( &s1, buf_valeur );
    #ifdef TIC_MODE_STANDARD
        // le separateur précédant le checksum est compté en mode standard mais pas en mode historique
        s1 += TIC_SEPARATOR;
    #endif

    // vérifie le checksum
    tic_char_t checksum = ( s1 & 0x3F ) + 0x20;   // voir doc linky enedis 
    if ( checksum != buf_checksum[0] )
    {
        ESP_LOGE( TAG, "Checksum incorrect pour %s. attendu=%#x calculé=%#x  (s1=%#x)", buf_etiquette, buf_checksum[0], checksum, s1 );
        return TIC_ERR_CHECKSUM;
    }

    // alloue un nouveau dataset et copie les données 
    tic_dataset_t *ds = (tic_dataset_t *) calloc(1, sizeof(tic_dataset_t));
    if (ds == NULL)
    {
        return TIC_ERR_MEMORY;
    }
    strncpy( ds->etiquette, buf_etiquette, TIC_SIZE_ETIQUETTE );
    strncpy( ds->valeur, buf_valeur, TIC_SIZE_VALUE );

    // horodate en option
    if( buf_horodate != NULL)
    {
        strncpy( ds->horodate, buf_horodate, TIC_SIZE_VALUE );
    }
    
    // ajoute le nouveau dataset à la liste
    ds->next = td->datasets;
    td->datasets = ds;

    // mise à jour afficheur oled
    affiche_dataset( td, ds );

    return TIC_OK;
}


static tic_error_t frame_start( tic_decoder_t *td ) 
{
    //ESP_LOGD( TAG, "frame_start()" );
    if( td->datasets != NULL || td->stx_received != 0 )
    {
        ESP_LOGE( TAG, "Trame invalide : STX reçu avant ETX sur la trame courante" );
        return TIC_ERR_INVALID_CHAR;
    }
    td->stx_received = 1;
    // todo -> creer un tache de surveillance de la memoire, ou tester les outils d'analyse ESP
    //ESP_LOGD( TAG, "Free memory: %d bytes", esp_get_free_heap_size());
    return TIC_OK;
}


static tic_error_t frame_end( tic_decoder_t *td )
{
    // monitoring sur la console serie
    ESP_LOGD( TAG, "Avant tri" );
    tic_dataset_print( td->datasets );

    td->datasets = tic_dataset_sort( td->datasets );
    ESP_LOGD( TAG, "Après tri" );
    tic_dataset_print( td->datasets );


    // signale la réception de données UART
    status_rcv_tic_frame( 0 );
    
 
    //uint32_t nb = tic_dataset_count( td->datasets );
    //uint32_t size = tic_dataset_size( td->datasets );
    //ESP_LOGI( TAG, "Trame de %d datasets %d bytes (%p)", nb, size, td->datasets );

    tic_error_t ret = TIC_OK;
    BaseType_t send_ok = xQueueSend( td->to_mqtt, &(td->datasets), 10 );
    if( send_ok == pdTRUE )
    {
        // les datasets devront être free() par le recepteur mqtt_client
        td->datasets = NULL;
    }
    else
    {
        ESP_LOGE( TAG, "Queue pleine : impossible d'envoyer la trame vers mqtt_client " );
        ret = TIC_ERR_QUEUEFULL;
    }
    reset_decoder( td );
    return ret;
}


static tic_error_t process_separator( tic_decoder_t *td, const tic_char_t ch )
{
    //ESP_LOGD(  TAG, "separator_received" );
    if ( td->cur_buf == td->buf0 )
    {
        //ESP_LOGD(  TAG, "  shift from buf0 (used %d bytes) to buf1", strlen(td->buf0) );
        td->cur_buf = td->buf1;
        td->cur_buf_size = TIC_SIZE_BUF1;
    }
    else if ( td->cur_buf == td->buf1 )
    {
        //ESP_LOGD(  TAG, "  shift from buf1 (used %d bytes) to buf2", strlen(td->buf1) );
        td->cur_buf = td->buf2;
        td->cur_buf_size = TIC_SIZE_BUF2;
    }
    else if ( td->cur_buf == td->buf2 )
    {
        //ESP_LOGD(  TAG, "  shift from buf2 (used %d bytes) to buf3", strlen(td->buf2) );
        td->cur_buf = td->buf3;
        td->cur_buf_size = TIC_SIZE_BUF3;
    }
    else
    {
        ESP_LOGE( TAG, "Données invalides : trop d'élements dans un dataset" );
        ESP_LOGE( TAG, "cur_buf: %p, buf0: %p, buf1: %p, buf2: %p, buf3: %p", td->cur_buf, td->buf0, td->buf1, td->buf2, td->buf3 );
        return TIC_ERR_OVERFLOW;
    }
    return TIC_OK;
}


static tic_error_t process_data( tic_decoder_t *td, const tic_char_t ch )
{
    // nombre de caractères déja reçus
    size_t pos = strlen( td->cur_buf );

    // cas particulier pour le séparateur 
    if ( ch == TIC_SEPARATOR && pos > 0)
    {
        return process_separator( td, ch );
    }

    // ajoute le caractère si le buffer n'est pas plein
    if ( pos < td->cur_buf_size )
    {
        td->cur_buf[pos] = ch;
    }
    else
    {
        ESP_LOGE( TAG, "tic_decoder_t overflow. Buffers trop petits (cur_buf_size=%d)", td->cur_buf_size );
        return TIC_ERR_OVERFLOW;
    }

    return TIC_OK;
}


static tic_error_t process_char( tic_decoder_t *td, const tic_char_t ch )  {

    // ESP_LOGD( TAG, "process_char() : '%c'", ch );
    tic_error_t ret = TIC_OK;

    // ignore toutes les données tant que STX n'est pas reçu
    if ( (ch != CHAR_STX) && (td->stx_received == 0) )
    {
        ESP_LOGD( TAG, "Attente début de trame - caractère '%c' ignoré ", ch );
        return ret;
    }

    switch ( ch ) { 
        case CHAR_STX:
            // start of frame - reset state machine & buffers
            ret = frame_start( td );
            break;
        case CHAR_ETX:
            // end of frame - send event or signal to antoher task 
            ret = frame_end( td );
            break;
        case CHAR_LF:
            // start of a dataset - reset buffers and flags
            ret = dataset_start( td );
            break;
        case CHAR_CR:
            // end of dataset 
            ret = dataset_end( td );
            break;
        default:
            // autres caractères : label, timestamp, value ou separateur
            ret = process_data( td, ch );
    }
    return ret;
}


static tic_error_t process_raw_data( tic_decoder_t* td, const tic_char_t *buf, size_t len )
{
    tic_error_t err = TIC_OK;
    uint32_t i;
    for( i=0; ( err==TIC_OK && i<len ) ; i++ )
    {
        err = process_char( td, buf[i] );
    }
    return err;
}


void tic_decode_task( void *pvParams )
{
    tic_taskdecode_params_t *params = (tic_taskdecode_params_t *)pvParams;

    // donnees internes du decodeur
    tic_decoder_t td = {
        .to_mqtt = params->to_mqtt,
        .to_ticled = params->to_ticled,
        .to_oled = params->to_oled
    };

    // buffer pour recevoir les données de la tache uart_events
    tic_char_t *rx_buf = malloc( RX_BUF_SIZE );
    if( rx_buf == NULL )
    {
        ESP_LOGD( TAG, "malloc() failed" );
        return;
    }

    tic_error_t err;
    for(;;) {
        size_t n = xStreamBufferReceive( params->from_uart, rx_buf, RX_BUF_SIZE, portMAX_DELAY );

        err = process_raw_data( &td, rx_buf, n );
        if( err != TIC_OK )
        {
            ESP_LOGE(TAG, "tic decoder error %#0x", err);
            reset_decoder( &td );
        }
    }
}


 /***
  * Create a task to decode teleinfo raw bytestream received from uart
  */
void tic_decode_start_task( StreamBufferHandle_t from_uart, QueueHandle_t mqtt_queue, EventGroupHandle_t to_ticled, QueueHandle_t to_oled )
{

    esp_log_level_set( TAG, ESP_LOG_DEBUG );

    tic_taskdecode_params_t *tic_task_params = malloc( sizeof(tic_taskdecode_params_t) );
    if( tic_task_params == NULL )
    {
        ESP_LOGE( TAG, "malloc() failed" );
        return;
    }

    tic_task_params->from_uart = from_uart;
    tic_task_params->to_mqtt = mqtt_queue;
    tic_task_params->to_ticled = to_ticled;
    tic_task_params->to_oled = to_oled;
    xTaskCreate(tic_decode_task, "tic_decode_task", 4096, (void *)tic_task_params, 12, NULL);
}