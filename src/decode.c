#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/stream_buffer.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mqtt_client.h"

#include "errors.h"
#include "decode.h"
#include "process.h"
#include "flags.h"
#include "status.h"
#include "ticled.h"

static const char *TAG = "decode.c";

#define CHAR_STX  0x02    //   start of text - début d'une trame
#define CHAR_ETX  0x03    //   end of text - fin d'une trame
#define CHAR_CR   '\r'
#define CHAR_LF   '\n'

#define TIC_MODE_STANDARD 1

// separateur dans un groupe de données
#ifdef TIC_MODE_HISTORIQUE
    #define TIC_SEPARATOR  ' '    /* SPACE */
#elif defined TIC_MODE_STANDARD
    #define TIC_SEPARATOR  0x09    /* TAB */
#endif

// alias pour les tailles de buffers
#define TIC_SIZE_BUF0 TIC_SIZE_ETIQUETTE
#define TIC_SIZE_BUF1 TIC_SIZE_VALUE
#define TIC_SIZE_BUF2 TIC_SIZE_VALUE
#define TIC_SIZE_BUF3 TIC_SIZE_CHECKSUM

/*
typedef struct tic_taskdecode_params_s {
    StreamBufferHandle_t from_uart;
    QueueHandle_t to_process;
} tic_taskdecode_params_t;
*/

StreamBufferHandle_t s_to_decoder;

// tic_frame_s contient les données d'une trame en cours de réception
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

tic_error_t tic_dataset_print( tic_dataset_t *ds )
{
    // ESP_LOGD( TAG, "print_datasets()");
    char flags_str[3];
    while( ds != NULL )
    {
        flags_str[0]= '.';
        flags_str[1]= '.';
        flags_str[2]=0;    // null-terminated
        if( ds->flags & TIC_DS_PUBLISHED )
        {
            flags_str[0]= ( ds->flags & TIC_DS_NUMERIQUE ) ? 'N' : 'S';
            if( ds->flags & TIC_DS_HAS_TIMESTAMP )
                flags_str[1]= 'H';
        }
        ESP_LOGD( TAG, "%8.8s %s %s %s", ds->etiquette, flags_str, ds->horodate, ds->valeur );
        ds = ds->next;
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


static tic_dataset_t* insert_sort( tic_dataset_t *sorted, tic_dataset_t *ds)
{
    assert( ds != NULL );           // l'insertion de NULL est invalide
    assert( ds->next == NULL );     // ds doit être un element isolé, le ptr sur le suivant doit rester chez l'appelant

    tic_dataset_t *item = sorted;

    while( item != NULL )
    {
        if( strcmp( ds->etiquette, item->etiquette ) < 0 )
        {
            // ds est plus petit que l'élément courant,
            assert( item == sorted ); // impossible sauf pour le premier element de la liste triée
            ds->next = item;          //  insere avant
            sorted = ds;
            break;
        }
        if ( (item->next == NULL) || (strcmp( ds->etiquette, item->next->etiquette ) <= 0 ) )
        {
            // ds est plus petit que l'élément suivant (ou pas d'élement suivant)
            ds->next = item->next;    // insere après
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
        // copie le ptr vers l'item suivant car ds->next va être modifié lors de son insertion dans 'sorted'
        ds_next = ds->next;
        ds->next=NULL;

        sorted = insert_sort( sorted, ds );
        ds = ds_next;
    }
    return sorted;
}


static void reset_decoder( tic_decoder_t *td )
{
    //ESP_LOGD( TAG, "reset_decoder()");
    // desalloue les datasets
    tic_dataset_free( td->datasets );

    // met à 0 toutes les variables et buffers 
    memset( td, 0, sizeof(tic_decoder_t) );
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

/*
static void tic_decoder_debug_state( const tic_decoder_t *td )
{
    ESP_LOGE( TAG, "cur_buf: %p", td->cur_buf );
    ESP_LOGE( TAG, "buf0: [%s] (addr %p len %d)", td->buf0, td->buf0, strlen(td->buf0) );
    ESP_LOGE( TAG, "buf1: [%s] (addr %p len %d)", td->buf1, td->buf1, strlen(td->buf1) );
    ESP_LOGE( TAG, "buf2: [%s] (addr %p len %d)", td->buf2, td->buf2, strlen(td->buf2) );
    ESP_LOGE( TAG, "buf3: [%s] (addr %p len %d)", td->buf3, td->buf3, strlen(td->buf3) );
}
*/

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

    // verifie que le checksum reçu est un caractere unique
    size_t checksum_len = strlen(buf_checksum);
    if( checksum_len != 1 )
    {
        ESP_LOGE( TAG, "Checksum reçu [%s] a une longueur %d differente de 1", buf_checksum, checksum_len );
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
        ESP_LOGE( TAG, "Checksum incorrect pour %s. attendu=%#x calculé=%#x  (s1=%#lx)", buf_etiquette, buf_checksum[0], checksum, s1 );
        //tic_decoder_debug_state( td );
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
    
    // ajoute les flags
    tic_dataset_flags_t flags;
    if( tic_get_flags( ds->etiquette, &flags) == TIC_OK )
    {
        ds->flags = flags;
    }
    else
    {
        // Completer tic_flags.c si cette erreur se produit
        ESP_LOGE( TAG, "Donnee %s inconnue diffusée par la TIC", ds->etiquette);
    }

    // ajoute le nouveau dataset à la liste
    //ds->next = td->datasets;
    //td->datasets = ds;
    td->datasets = insert_sort( td->datasets, ds );

    // mise à jour afficheur oled
    //affiche_dataset( td, ds );

    return TIC_OK;
}


static tic_error_t frame_start( tic_decoder_t *td ) 
{
    //ESP_LOGD( TAG, "frame_start()" );
    if( td->datasets != NULL || td->stx_received != 0 )
    {
        ESP_LOGE( TAG, "Trame incomplète : STX reçu avant ETX" );
        return TIC_ERR_INVALID_CHAR;
    }
    td->stx_received = 1;
    // todo -> creer un tache de surveillance de la memoire, ou tester les outils d'analyse ESP
    ESP_LOGD( TAG, "Free memory: %lu bytes", esp_get_free_heap_size());
    return TIC_OK;
}


static tic_error_t frame_end( tic_decoder_t *td )
{
    //td->datasets = tic_dataset_sort( td->datasets );

    // monitoring sur la console serie
    tic_dataset_print( td->datasets );

    // signale la réception de données UART
    status_rcv_tic_frame( 0 );

    //uint32_t nb = tic_dataset_count( td->datasets );
    //uint32_t size = tic_dataset_size( td->datasets );
    //ESP_LOGI( TAG, "Trame de %d datasets %d bytes (%p)", nb, size, td->datasets );

    tic_error_t ret = TIC_OK;
    BaseType_t send_ok = process_receive_datasets( td->datasets );
    if( send_ok == pdTRUE )
    {
        // les datasets devront être free() par le recepteur ( process_task )
        td->datasets = NULL;
    }
    else
    {
        ESP_LOGE( TAG, "Queue pleine : impossible d'envoyer la trame vers mqtt_client " );
        ret = TIC_ERR_QUEUEFULL;
    }
    reset_decoder( td );  // appelle tic_dataset_free( td->datasets )
    return ret;
}


static tic_error_t decode_separator( tic_decoder_t *td, const tic_char_t ch )
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


static tic_error_t decode_data( tic_decoder_t *td, const tic_char_t ch )
{
    // nombre de caractères déja reçus
    size_t pos = strlen( td->cur_buf );

    // cas particulier pour le séparateur 
    if ( ch == TIC_SEPARATOR )
    {
        return decode_separator( td, ch );
    }

    // ajoute le caractère si le buffer n'est pas plein
    if ( pos < td->cur_buf_size )
    {
        td->cur_buf[pos] = ch;
    }
    else
    {
        ESP_LOGE( TAG, "tic_decoder_t overflow. Buffers trop petits (cur_buf_size=%d)", td->cur_buf_size );
        //tic_decoder_debug_state( td );
        return TIC_ERR_OVERFLOW;
    }

    return TIC_OK;
}


static tic_error_t decode_char( tic_decoder_t *td, const tic_char_t ch )  {

    // ESP_LOGD( TAG, "process_char() : '%c'", ch );
    tic_error_t ret = TIC_OK;

    // ignore toutes les données tant que STX n'est pas reçu
    if ( (ch != CHAR_STX) && (td->stx_received == 0) )
    {
        //ESP_LOGD( TAG, "Attente début de trame - caractère '%c' ignoré ", ch );
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
            ret = decode_data( td, ch );
    }
    return ret;
}


static tic_error_t decode_raw_data( tic_decoder_t* td, const tic_char_t *buf, size_t len )
{
    tic_error_t err = TIC_OK;
    uint32_t i;
    for( i=0; ( err==TIC_OK && i<len ) ; i++ )
    {
        err = decode_char( td, buf[i] );
    }
    return err;
}


void tic_decode_task( void *pvParams )
{
    // donnees internes du decodeur
    tic_decoder_t *td = (tic_decoder_t *)malloc( sizeof( tic_decoder_t) );
    // buffer pour recevoir les données de la tache uart_events 
    tic_char_t *rx_buf = malloc( RX_BUF_SIZE );
    if( (td==NULL) || (rx_buf==NULL) )
    {
        ESP_LOGD( TAG, "malloc() failed" );
        return;
    }
    reset_decoder( td );

    tic_error_t err;
    for(;;) {
        size_t n = xStreamBufferReceive( s_to_decoder, rx_buf, RX_BUF_SIZE, portMAX_DELAY );

        err = decode_raw_data( td, rx_buf, n );
        if( err != TIC_OK )
        {
            ESP_LOGE(TAG, "tic decoder error (%#0x)", err);
            reset_decoder( td );
        }
    }
}

// Appelé par la tâche uart_events.c pour envoyer les bytes reçus
size_t decode_receive_bytes( void *buf , size_t length )
{
    return xStreamBufferSend( s_to_decoder, buf, length, portMAX_DELAY);
}

// Create a task to decode teleinfo raw bytestream received from uart
void tic_decode_task_start( )
{
    esp_log_level_set( TAG, ESP_LOG_DEBUG );

    /*
    tic_taskdecode_params_t *tic_task_params = malloc( sizeof(tic_taskdecode_params_t) );
    if( tic_task_params == NULL )
    {
        ESP_LOGE( TAG, "malloc() failed" );
        return;
    }
    */

    // transfere le flux de données brutes depuis l'UART vers le decodeur
    s_to_decoder = xStreamBufferCreate( DECODE_RCV_BUFFER_SIZE, DECODE_RCV_BUFFER_TRIGGER );
    if( s_to_decoder == NULL )
    {
        ESP_LOGE( TAG, "Failed to create to_decoder StreamBuffer" );
    }

    xTaskCreate(tic_decode_task, "tic_decode_task", 4096, NULL, 12, NULL);
}