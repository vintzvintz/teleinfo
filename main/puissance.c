

#include <string.h>
#include <time.h>

#include "esp_log.h"

#include "errors.h"
#include "dataset.h"
#include "puissance.h"

static const char *TAG = "puissance.c";

#define TIC_LAST_POINTS_CNT  10

static const char *LABEL_ENERGIE_ACTIVE_TOTALE = "EAST";
static const char *LABEL_HORODATE = "DATE";

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
const east_point_t* get_east_point( int8_t i )
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

    char *end;
    *val = strtol( buf, &end, 10);
    if( end-buf != read_len)
    {
        ESP_LOGE( TAG, "erreur strtol() sur %s", buf);
        return TIC_ERR_BAD_DATA;
    }
    *val += offset;
    return TIC_OK;
}


static tic_error_t horodate_to_time_t( const char *horodate, time_t *unix_time)
{
    //ESP_LOGD( TAG, "horodate_to_time_t(%s)", horodate  );
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
    //ESP_LOGD( TAG, "horodate decodéee %s", timebuf );

    // renvoie le timestamp unix sur le pointeur fourni
    if( unix_time != NULL )
    {
        *unix_time = mktime( &tm );
    }
    return TIC_OK;
}


// construit un east_point_t à partir d'un dataset
static tic_error_t ds_to_point( east_point_t *pt, const dataset_t *ds )
{
    if( pt == NULL ) 
    {
        return TIC_ERR_MISSING_DATA;
    }

    pt->ts = 0;
    pt->east = 0;

    // cherche la valeur de l'index et l'horodate dans le dataset
    const dataset_t *ds_horodate = dataset_find( ds, LABEL_HORODATE );
    if( ds_horodate == NULL )
    {
        ESP_LOGE( TAG, "Donnee DATE manquante");
        return TIC_ERR_MISSING_DATA;
    }
 
    const dataset_t *ds_east_index = dataset_find( ds, LABEL_ENERGIE_ACTIVE_TOTALE );
    if( ds_east_index == NULL )
    {
        ESP_LOGE( TAG, "Donnee EAST manquante");
        return TIC_ERR_MISSING_DATA;
    }

    //ESP_LOGD( TAG, "ds_to_point() : horodate=%s index=%s", ds_horodate->horodate, ds_east_index->valeur );
    
    // traite l'horodate
    time_t ts;
    tic_error_t err = horodate_to_time_t( ds_horodate->horodate, &ts );
    if( err != TIC_OK )
    {
        return err;
    }

    // traite l'index
    char *end;
    int val = strtol( ds_east_index->valeur, &end, 10);
    if( *end != '\0')
    {
        ESP_LOGE( TAG, "erreur strtol() sur l'index EAST '%s'", ds_east_index->valeur );
        return TIC_ERR_BAD_DATA;
    }

    pt->ts = ts;
    pt->east = val;
    ESP_LOGD( TAG, "ds_to_point() : ts=%"PRIi64" east=%"PRIi32, pt->ts, pt->east );
    return TIC_OK;
}


tic_error_t puissance_new_trame( const dataset_t *ds )
{
    east_point_t pt = {0};

    // extrait l'index et l'horodate de la trame reçue
    tic_error_t err = ds_to_point(&pt, ds);
    if( err != TIC_OK  )
    {
        ESP_LOGW( TAG, "Trame ignorée - valeurs incorrectes");
        return err;
    }

    // compare avec le dernier point reçu 
    const east_point_t *prev = get_east_point( 0 );
    if( prev!=NULL && prev->east==pt.east )
    {
        //ESP_LOGD( TAG, "trame ignorée - index EAST inchangé %"PRIi32, pt.east );
        return TIC_OK;
    }

    add_east_point( &pt );  // ne renvoie jamais d'erreur
    ESP_LOGD( TAG, "trame ajoutée EAST=%"PRIi32, pt.east );
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