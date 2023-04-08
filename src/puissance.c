

#include <string.h>
#include <time.h>

#include "esp_log.h"

#include "errors.h"
#include "decode.h"
#include "puissance.h"

static const char *TAG = "puissance.c";

#define TIC_LAST_POINTS_CNT  10

static const char *LABEL_ENERGIE_ACTIVE_TOTALE = "EAST";
static const char *LABEL_HORODATE = "DATE";


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
    int8_t pos = (s_east_current - 1) % TIC_LAST_POINTS_CNT;   // les points sont stockés "à l'envers"
    ESP_LOGD( TAG, "add_east_point() s_east_current=%"PRIi8" pos=%"PRIi8" ts=%"PRIi64" east=%"PRIi32, s_east_current, pos, pt->ts, pt->east );
    s_east_rb[pos].east = pt->east;
    s_east_rb[pos].ts = pt->ts;
}


int puissance_get ( uint8_t n )
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


tic_error_t puissance_new_trame( const tic_dataset_t *ds )
{
    east_point_t *pt = NULL;

    const tic_dataset_t *ds_horodate = tic_dataset_find( ds, LABEL_HORODATE );
    if( ds_horodate == NULL )
    {
        ESP_LOGE( TAG, "Donnee DATE manquante");
        return TIC_ERR_MISSING_DATA;
    }
 
    const tic_dataset_t *ds_east_index = tic_dataset_find( ds, LABEL_ENERGIE_ACTIVE_TOTALE );
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