

#include <string.h>
#include <time.h>

#include "esp_log.h"

#include "tic_types.h"
#include "dataset.h"
#include "puissance.h"

static const char *TAG = "puissance.c";

#define TIC_LAST_POINTS_CNT  10

static const char *LABEL_PACT_PREFIX = "PACT";


typedef struct point_east_s {
    time_t ts;      // timestamp du point
    int32_t east;                       // index d'energie active soutiree totale du compteur
} east_point_t;


// conserve les derniers points reçus
static east_point_t s_east_rb[TIC_LAST_POINTS_CNT];    // ring buffer
static int8_t s_east_current;


void puissance_init()
{
    memset( &s_east_rb, 0, TIC_LAST_POINTS_CNT*sizeof(east_point_t));
    s_east_current = 0;
}

// recupere un point dans le ring buffer
// i=0 : le plus récent   
//i=cnt : le plus ancien
static const east_point_t* get_east_point( int8_t i )
{
    int8_t idx = ( i + s_east_current ) % TIC_LAST_POINTS_CNT;
    return &(s_east_rb[idx]);
}

// ajoute un nouveau point dans le ring buffer
static void add_east_point( const east_point_t * pt )
{
    // -1 pour stocker les points dans l'ordre inversé
    int8_t pos = (s_east_current + TIC_LAST_POINTS_CNT - 1) % TIC_LAST_POINTS_CNT;
    ESP_LOGD( TAG, "add_east_point() s_east_current=%"PRIi8" pos=%"PRIi8" ts=%"PRIi64" east=%"PRIi32, s_east_current, pos, pt->ts, pt->east );
    s_east_rb[pos].east = pt->east;
    s_east_rb[pos].ts = pt->ts;
    s_east_current = pos;
}


int32_t puissance_get ( uint8_t n )
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
        //ESP_LOGD( TAG, "p_active(%d) indisponible", n);
        return -1;
    }

    int32_t energie = pN->east - p0->east;
    time_t duree = pN->ts - p0->ts;

    if( duree == 0 )
    {
        ESP_LOGD( TAG, "p_active(%d) indisponible : deux index avec horodate identique", n);
        return -1;
    }

    return (3600*energie)/duree;    // energie est en Watt.heure, on veut des Watt.seconde
}

// calcule les puissances actives et revoie les resultats sous forme de datasets 
dataset_t * puissance_get_all()
{
    dataset_t * ret = NULL;

    // pour le debug
    char etat[TIC_LAST_POINTS_CNT+2];
    etat[0] = '[';
    etat[TIC_LAST_POINTS_CNT] = ']';
    etat[TIC_LAST_POINTS_CNT+1] = '\0';

    for( uint8_t i=1; i<TIC_LAST_POINTS_CNT; i++ )
    {
        int32_t p = puissance_get( i );
        
        if( p<0 )
        {
            etat[i]='_';
            continue;
        }
        etat[i] = 'x';
        
        dataset_t *ds = dataset_alloc();
        snprintf( ds->etiquette, TIC_SIZE_ETIQUETTE, "%s%02"PRIi8, LABEL_PACT_PREFIX, i );
        snprintf( ds->valeur, TIC_SIZE_VALUE, "%"PRIi32, p );
        ds->flags = TIC_DS_NUMERIQUE | TIC_DS_PUBLISHED;

        ret = dataset_append( ret, ds );
    }
    ESP_LOGD(TAG, "Puissances actives disponibles %s", etat);
    return ret;
}


tic_error_t puissance_incoming_data( const tic_data_t *data )
{
    east_point_t pt = {
        .ts = data->horodate,
        .east = data->index_energie
    };

    // compare avec le dernier point reçu 
    const east_point_t *prev = get_east_point( 0 );
    if( prev->east != pt.east )
    {
        add_east_point( &pt );  // ne renvoie jamais d'erreur
        ESP_LOGD( TAG, "nouvel index energie reçu %"PRIi32, pt.east );
    }
    return TIC_OK;
}


/*
void puissance_debug()
{
    uint8_t i;
    int32_t p_active;
    for( i=1; i< TIC_LAST_POINTS_CNT; i++ )
    {
        p_active = puissance_get(i);
        if( p_active >=0 )
        {
            ESP_LOGD( TAG,"Puissance active sur %d points = %"PRIi32, i, p_active);
        }
    }
}
*/