#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"


#include "tic_types.h"
#include "dataset.h"
#include "decode.h"
#include "process.h"
#include "status.h"

static const char *TAG = "decode.c";

#define CHAR_STX   0x02    //   start of text - début d'une trame
#define CHAR_ETX   0x03    //   end of text - fin d'une trame
#define CHAR_CR    '\r'
#define CHAR_LF    '\n'
#define CHAR_SPACE ' '
#define CHAR_TAB   '\t'

#define TIC_SEPARATOR_INCONNU     0x00
#define TIC_SEPARATOR_HISTORIQUE  CHAR_SPACE
#define TIC_SEPARATOR_STANDARD    CHAR_TAB


// alias pour les tailles de buffers
#define TIC_SIZE_BUF0 TIC_SIZE_ETIQUETTE
#define TIC_SIZE_BUF1 TIC_SIZE_VALUE
#define TIC_SIZE_BUF2 TIC_SIZE_VALUE
#define TIC_SIZE_BUF3 TIC_SIZE_CHECKSUM

#define TIC_MAX_DATASETS 99

#define INCOMING_QUEUE_SIZE  10
static QueueHandle_t s_incoming_bytes = NULL;

typedef struct {
    tic_char_t *buf;
    size_t len;
    tic_mode_t mode;
} tic_bytes_t;


// tic_frame_s contient les données d'une trame en cours de réception
typedef struct tic_decoder_s {
    
    // mode historique ou standard
    tic_mode_t mode;
    tic_char_t sep;

    dataset_t *datasets;         // linked list des datasets complets reçus

    uint8_t stx_received;  // caractere start of frame recu ?


    // buffers pour le dataset en cours de reception
    tic_char_t buf0[TIC_SIZE_BUF0];   // etiquette
    tic_char_t buf1[TIC_SIZE_BUF1];   // horodate ou valeur
    tic_char_t buf2[TIC_SIZE_BUF2];   // valeur ou checksum reçu
    tic_char_t buf3[TIC_SIZE_BUF3];   // checksum reçu ou NULL

    // selecteur du buffer courant
    tic_char_t *cur_buf;
    size_t cur_buf_size;
} tic_decoder_t;


static void reset_decoder( tic_decoder_t *td )
{
    ESP_LOGD( TAG, "reset_decoder()");
    // conserve mode et separateur

    // desalloue les datasets
    dataset_free( td->datasets );
    td->datasets = NULL;

    // met à 0 toutes les variables et buffers 
    td->stx_received = 0;
    memset (td->buf0, 0 , sizeof(td->buf0));
    memset (td->buf1, 0 , sizeof(td->buf1));
    memset (td->buf2, 0 , sizeof(td->buf2));
    memset (td->buf3, 0 , sizeof(td->buf3));
    td->cur_buf = td->buf0;
    td->cur_buf_size = sizeof(td->buf0);
}


static tic_error_t decode_dataset_start( tic_decoder_t *td )
{
    //ESP_LOGD( TAG, "dataset_start()");

    // limite le nombre de datasets dans une trame
    if( dataset_count( td->datasets ) >= TIC_MAX_DATASETS )
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


static void tic_decoder_debug_state( const tic_decoder_t *td )
{
    ESP_LOGI( TAG, "mode=%d, sep=%#02x", td->mode, td->sep);
    ESP_LOGI (TAG, "stx_received=%d", td->stx_received);
    ESP_LOGI( TAG, "cur_buf=%p cur_buf_size=%d", td->cur_buf, td->cur_buf_size );
    ESP_LOGI( TAG, "buf0: [%s] (addr %p len %d)", td->buf0, td->buf0, strlen(td->buf0) );
    ESP_LOGI( TAG, "buf1: [%s] (addr %p len %d)", td->buf1, td->buf1, strlen(td->buf1) );
    ESP_LOGI( TAG, "buf2: [%s] (addr %p len %d)", td->buf2, td->buf2, strlen(td->buf2) );
    ESP_LOGI( TAG, "buf3: [%s] (addr %p len %d)", td->buf3, td->buf3, strlen(td->buf3) );
}


static tic_error_t decode_dataset_end( tic_decoder_t *td ) {
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
    s1 += td->sep;
    if( buf_horodate != NULL )
    {
        addbuf( &s1, buf_horodate );
        s1 += td->sep;
    }
    addbuf( &s1, buf_valeur );

    // le separateur précédant le checksum est compté en mode standard mais pas en mode historique
    if (td->mode == TIC_MODE_STANDARD)
    {
        s1 += td->sep;
    }

    // vérifie le checksum
    tic_char_t checksum = ( s1 & 0x3F ) + 0x20;   // voir doc linky enedis 
    if ( checksum != buf_checksum[0] )
    {
        ESP_LOGE( TAG, "Checksum incorrect pour %s. attendu=%#x calculé=%#x  (s1=%#lx)", buf_etiquette, buf_checksum[0], checksum, s1 );
        //tic_decoder_debug_state( td );
        return TIC_ERR_CHECKSUM;
    }

    // alloue un nouveau dataset et copie les données 
    dataset_t *ds = dataset_alloc();
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
    if( dataset_flags_definition( ds->etiquette, td->mode, &flags) == TIC_OK )
    {
        ds->flags = flags;
    }
    else
    {
        // Completer tic_flags.c si cette erreur se produit
        ESP_LOGW( TAG, "Donnee %s inconnue diffusée par la TIC", ds->etiquette);
    }

    // ajoute le nouveau dataset à la liste
    //ds->next = td->datasets;
    //td->datasets = ds;
    td->datasets = dataset_insert( td->datasets, ds );

    // mise à jour afficheur oled
    //affiche_dataset( td, ds );

    return TIC_OK;
}


static tic_error_t decode_frame_start( tic_decoder_t *td ) 
{
    //ESP_LOGD( TAG, "frame_start()" );
    if( td->datasets != NULL || td->stx_received != 0 )
    {
        ESP_LOGE( TAG, "Trame incomplète : STX reçu avant ETX" );
        return TIC_ERR_INVALID_CHAR;
    }
    td->stx_received = 1;
    return TIC_OK;
}


static tic_error_t decode_frame_end( tic_decoder_t *td )
{
    //td->datasets = tic_dataset_sort( td->datasets );

    // monitoring sur la console serie
    //dataset_print( td->datasets );
    ESP_LOGD( TAG, "Trame de %"PRIi32" datasets reçue", dataset_count(td->datasets) );

    // signale la réception correcte d'une trame
    status_update_tic_mode( td->mode, 0 );

    //uint32_t nb = tic_dataset_count( td->datasets );
    //uint32_t size = tic_dataset_size( td->datasets );
    //ESP_LOGI( TAG, "Trame de %d datasets %d bytes (%p)", nb, size, td->datasets );

    tic_error_t err = process_receive_datasets( td->datasets );
    if( err == TIC_OK )
    {
        // les datasets devront être free() par le recepteur ( process_task )
        td->datasets = NULL;
    }
    else
    {
        ESP_LOGE( TAG, "Queue pleine : impossible d'envoyer la trame vers process_task " );
    }
    reset_decoder( td );  // appelle tic_dataset_free( td->datasets )

    // todo -> creer un tache de surveillance de la memoire, ou tester les outils d'analyse ESP
    ESP_LOGD( TAG, "Free memory: %lu bytes", esp_get_free_heap_size());

    return err;
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
    if ( ch == td->sep )
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
        tic_decoder_debug_state( td );
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
            ret = decode_frame_start( td );
            break;
        case CHAR_ETX:
            // end of frame - send event or signal to antoher task 
            ret = decode_frame_end( td );
            break;
        case CHAR_LF:
            // start of a dataset - reset buffers and flags
            ret = decode_dataset_start( td );
            break;
        case CHAR_CR:
            // end of dataset 
            ret = decode_dataset_end( td );
            break;
        default:
            // autres caractères : label, timestamp, value ou separateur
            ret = decode_data( td, ch );
    }
    return ret;
}


static tic_error_t decode_raw_data( tic_decoder_t* td, const tic_char_t *buf, size_t len )
{
    assert ((td->mode == TIC_MODE_HISTORIQUE) || (td->mode!=TIC_MODE_HISTORIQUE));
    tic_error_t err = TIC_OK;
    uint32_t i;
    for( i=0; ( err==TIC_OK && i<len ) ; i++ )
    {
        err = decode_char( td, buf[i] );
    }
    return err;
}


static tic_error_t decode_set_mode( tic_decoder_t* td, tic_mode_t mode )
{

    if ((mode != TIC_MODE_HISTORIQUE) && (mode!=TIC_MODE_STANDARD) )
    {
        ESP_LOGW( TAG, "decode_set_mode( %d )", mode);
    }


    if ((td->mode == TIC_MODE_INCONNU) || (mode != td->mode) )
    {
        reset_decoder(td);
    }

    switch(mode)
    {
        case TIC_MODE_HISTORIQUE:
            td->sep = TIC_SEPARATOR_HISTORIQUE;
            td->mode = TIC_MODE_HISTORIQUE;
        break;
        case TIC_MODE_STANDARD:
            td->sep = TIC_SEPARATOR_STANDARD;
            td->mode = TIC_MODE_STANDARD;
        break;
        default:
            td->sep = TIC_SEPARATOR_INCONNU;
            td->mode = TIC_MODE_INCONNU;
            ESP_LOGE (TAG, "mode tic %0#x inconnu", mode);
            return TIC_ERR;
    }
    return TIC_OK;
}


void tic_decode_task( void *pvParams )
{
    ESP_LOGD( TAG, "tic_decode_task()" );
    assert( s_incoming_bytes );

    // donnees internes du decodeur
    tic_decoder_t *td = calloc(1, sizeof(tic_decoder_t));
    if (td==NULL)
    {
        ESP_LOGD( TAG, "calloc() failed" );
        return;
    }
    reset_decoder( td );

    tic_error_t err;
    tic_bytes_t packet = {0};

    for(;;) {
        free (packet.buf);   // alloué dans uart_events
        packet.buf=NULL;
        if( xQueueReceive (s_incoming_bytes, &packet, portMAX_DELAY) != pdPASS )
        {
            ESP_LOGE(TAG, "xQueueReceive() failed");
            continue;
        }

        // ignore les packets vides
        if (packet.len==0 || packet.buf==NULL)
        {
            continue;
        }

        // mode historique ou standard ?
        err = decode_set_mode(td, packet.mode) != TIC_OK;
        if( err != TIC_OK )
        {
            continue;
        }

        err = decode_raw_data( td, packet.buf, packet.len );
        if( err != TIC_OK )
        {
            ESP_LOGE(TAG, "tic decoder error (%#0x)", err);
            reset_decoder( td );
        }
    }
}

// Appelé par la tâche uart_events.c pour envoyer les bytes reçus
tic_error_t decode_incoming_bytes( tic_char_t *buf , size_t len, tic_mode_t mode )
{
    if( s_incoming_bytes == NULL )
    {
        ESP_LOGD( TAG, "decodeur non initialisé" );
        return 0;
    }

    tic_bytes_t packet = {
        .buf = buf,
        .len = len,
        .mode = mode
    };
    if (xQueueSend (s_incoming_bytes, &packet, portMAX_DELAY) != pdPASS)
    {
        ESP_LOGE (TAG, "xQueueSend() failed");
        return TIC_ERR_QUEUEFULL;
    }

    //return xStreamBufferSend( s_to_decoder, buf, length, portMAX_DELAY);
    return TIC_OK;
}

// Create a task to decode teleinfo raw bytestream received from uart
void tic_decode_task_start( )
{
    //esp_log_level_set( TAG, ESP_LOG_DEBUG );

    /*
    tic_taskdecode_params_t *tic_task_params = malloc( sizeof(tic_taskdecode_params_t) );
    if( tic_task_params == NULL )
    {
        ESP_LOGE( TAG, "malloc() failed" );
        return;
    }
    */

    // transfere le flux de données brutes depuis l'UART vers le decodeur
    s_incoming_bytes = xQueueCreate (INCOMING_QUEUE_SIZE, sizeof(tic_bytes_t));
    if (s_incoming_bytes == NULL)
    {
        ESP_LOGE (TAG, "xQueueCreate() failed");
    }

    xTaskCreate(tic_decode_task, "tic_decode_task", 4096, NULL, 12, NULL);
}