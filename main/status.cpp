
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

#include "esp_netif.h"      // pour les IP_EVENT
#include "esp_log.h"
#include "esp_wifi.h"

// from Kconfig
#ifdef CONFIG_TIC_CONSOLE


#include "tic_types.h"
#include "event_loop.h"
#include "uart_events.h"
#include "status.h"

static const char *TAG = "status.cpp";

static const char *FMT_UART            = "UART rx_rate=%d tic_signal=%d\n";
static const char *FMT_UART_NOSIGNAL   = "UART rx_rate=%d no signal\n";
static const char *FMT_TICMODE         = "TIC  mode %s\n";
static const char *FMT_MQTT            = "MQTT %s\n";
static const char *FMT_WIFI            = "WIFI ssid '%s' chan %d rssi %d\n";
static const char *FMT_WIFI_NOCNX      = "WIFI not connected\n";
static const char *FMT_WIFI_ERROR      = "WIFI error\n";
static const char *FMT_IP_INFO         = "IP   addr %s mask %s gw %s\n";
static const char *FMT_TIME            = "TIME %s  (sntp server %s)\n";


static const char *STATUS_HISTORIQUE = "historique";
static const char *STATUS_STANDARD   = "standard";
static const char *STATUS_INCONNU = "inconnu";


class TicStatus
{
private:

    // pour affichage combiné baudrate/mode sur une seule ligne
    tic_mode_t m_mode;
    int m_baudrate;      // 0:inconnu;   1200:historique;   9600:standard
    char *m_mqtt_status;
    esp_netif_ip_info_t m_ip_info;

    // protection des acces aux variables membres
    portMUX_TYPE m_spinlock;
    tic_mode_t get_tic_mode();
    void set_tic_mode( tic_mode_t mode );

    int get_baudrate();
    void set_baudrate (int rate);

    tic_error_t get_mqtt_status( char *buf, size_t len );
    tic_error_t set_mqtt_status (const char *status);

    void set_ip_info( const esp_netif_ip_info_t *ip_info );
    void get_ip_info( esp_netif_ip_info_t *ip_info );

    // handlers enregistrés sur les event loops ESP - ne peuvent pas être des fonctions membre
    static void static_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data );
    static void static_status_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data );

    // reception des evènements entrants
    void ip_event_handler( esp_event_base_t event_base, int32_t event_id, void *event_data );
    void status_event_handler( esp_event_base_t event_base, int32_t event_id, void *event_data );

    // printers pour chaque element de statut
    void print_time();
    void print_tic_mode();
    void print_uart();
    void print_mqtt();
    void print_wifi();
    void print_ip_addr();

public: 
    explicit TicStatus( );
    ~TicStatus();
    tic_error_t setup();
    void print_status();
};


static TicStatus *s_tic_status = NULL;


TicStatus::TicStatus() :
      m_mode(TIC_MODE_INCONNU)
    , m_baudrate(0)               // 0:inconnu;   1200:historique;   9600:standard
    , m_mqtt_status(NULL)
    , m_spinlock()
{
    set_ip_info(NULL);
}

TicStatus::~TicStatus( )
{
    if ( m_mqtt_status )
    {
        free( m_mqtt_status );
    }
}


tic_error_t TicStatus::setup()
{
    // handler pour recevoir les evenements sur l'event loop crée dans status.c
    tic_error_t err = tic_register_event_handler( ESP_EVENT_ANY_ID, &static_status_event_handler, this );

    // handler pour les IP_EVENT sur l'eventloop par défaut du système
    esp_err_t err2 = esp_event_handler_instance_register( IP_EVENT, ESP_EVENT_ANY_ID, &static_ip_event_handler, this, NULL );

    if ( (err != TIC_OK)  || (err2 != ESP_OK) )
    {
        ESP_LOGE( TAG, "tic_register_event_handler() erreur %d", err );
        return TIC_ERR_APP_INIT;   // message loggué par status_register_event_handler()
    }
    return TIC_OK;
}


tic_mode_t TicStatus::get_tic_mode()
{
    tic_mode_t mode;
    // accès concurrent possible car set_mode() est appelé par une autre tâche
    // via le event_handler
    taskENTER_CRITICAL ( &m_spinlock );
    mode = m_mode;
    taskEXIT_CRITICAL ( &m_spinlock );
    return mode;
}

void TicStatus::set_tic_mode( tic_mode_t mode)
{
    taskENTER_CRITICAL ( &m_spinlock );
    m_mode = mode;
    taskEXIT_CRITICAL ( &m_spinlock );
}

int TicStatus::get_baudrate()
{
    int rate;
    taskENTER_CRITICAL ( &m_spinlock );
    rate = m_baudrate;
    taskEXIT_CRITICAL ( &m_spinlock );
    return rate;
}

void TicStatus::set_baudrate (int rate)
{
    taskENTER_CRITICAL ( &m_spinlock );
    m_baudrate = rate;
    taskEXIT_CRITICAL ( &m_spinlock );
}


tic_error_t TicStatus::get_mqtt_status( char *buf, size_t len )
{
    taskENTER_CRITICAL ( &m_spinlock );
    strncpy ( buf, m_mqtt_status ? m_mqtt_status : "", len );
    taskEXIT_CRITICAL ( &m_spinlock );
    return TIC_OK;
}

tic_error_t TicStatus::set_mqtt_status (const char *status)
{
    size_t len = strlen (status)+1;
    char *new_buf = (char *)calloc ( 1, len );
    if (!new_buf)
    {
        ESP_LOGE ( TAG, "calloc() failed" );
        return TIC_ERR_OUT_OF_MEMORY;
    }
    strncpy ( new_buf, status, len );

    taskENTER_CRITICAL ( &m_spinlock );
    char *old_buf = m_mqtt_status;
    m_mqtt_status = new_buf;
    taskEXIT_CRITICAL ( &m_spinlock );

    if( old_buf )
    {
        free ( old_buf );
    }
    return TIC_OK;
}


void TicStatus::set_ip_info( const esp_netif_ip_info_t *ip_info )
{
    taskENTER_CRITICAL( &m_spinlock );
    if( ip_info == NULL )
    {
        memset( &m_ip_info, 0, sizeof(m_ip_info) );
    }
    else
    {
        memcpy( &m_ip_info, ip_info, sizeof(m_ip_info) );
    }
    taskEXIT_CRITICAL( &m_spinlock );
}



void TicStatus::get_ip_info( esp_netif_ip_info_t *ip_info )
{
    assert( ip_info != NULL );

    taskENTER_CRITICAL( &m_spinlock );
    memcpy( ip_info, &m_ip_info, sizeof(*ip_info) );
    taskEXIT_CRITICAL( &m_spinlock );
}


void TicStatus::status_event_handler( esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    assert ( event_base==STATUS_EVENTS);

    int baudrate = 0;
    const char *status = NULL;
    tic_data_t *data = NULL;

    switch( event_id )
    {
        case STATUS_EVENT_BAUDRATE:
            baudrate = *(int*)event_data;
            //ESP_LOGD( TAG, "STATUS_EVENT_BAUDRATE baudrate=%d", baudrate);
            set_baudrate ( baudrate );
            break;
        case STATUS_EVENT_TIC_DATA:
            data = (tic_data_t *)event_data;
            //ESP_LOGD( TAG, "STATUS_EVENT_TIC_MODE mode=%#x", data->mode);
            set_tic_mode ( data->mode );
            break;
        case STATUS_EVENT_MQTT:
            status = (const char *)event_data;
            //ESP_LOGD( TAG, "STATUS_EVENT_MQTT %s", status);
            set_mqtt_status ( status );
            break;
        //default:
            //ESP_LOGD( TAG, "STATUS_EVENT id %" PRIi32, event_id);
    }
}


void TicStatus::ip_event_handler( esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    assert ( event_base==IP_EVENT );

    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;

    switch( event_id ) 
    {
    case IP_EVENT_STA_GOT_IP:               // !< station got IP from connected AP 
        set_ip_info( &(event->ip_info) );
        break;
    case IP_EVENT_STA_LOST_IP:              // !< station lost IP and the IP is reset to 0
        set_ip_info( NULL );
        break;
    default:
        ESP_LOGD( TAG, "IP_EVENT id=%#lx", event_id );
    }
}


// fonction statique appelée par les event loop
void TicStatus::static_status_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    // handler_arg doit être un pointeur vers TicStatus
    TicStatus *ts = (TicStatus *)arg;
    ts->status_event_handler (event_base, event_id, event_data);
}



// fonction statique appelée par les event loop
void TicStatus::static_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    // handler_arg doit être un pointeur vers TicDisplay
    TicStatus *ts = (TicStatus *)arg;
    ts->ip_event_handler (event_base, event_id, event_data);
}


void TicStatus::print_time()
{
#ifdef CONFIG_TIC_SNTP
    time_t now = time(NULL);
    struct tm timeinfo;
    char buf[60];
    localtime_r( &now, &timeinfo );
    strftime( buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo );
    printf( FMT_TIME, buf, CONFIG_TIC_SNTP_SERVER );
#endif // CONFIG_TIC_SNTP
}

void TicStatus::print_uart()
{
//    ESP_LOGD( TAG, "TicStatus::print_uart()" );
    int rx = uart_get_rx_baudrate();       // baudrate configuré sur l'UART
    int signal = get_baudrate();           // 0 si pas de signal
    if( signal > 0 )  {
        printf( FMT_UART, rx, signal );
    } else {
        printf( FMT_UART_NOSIGNAL, rx );
    }
}

void TicStatus::print_tic_mode()
{
//    ESP_LOGD( TAG, "TicStatus::print_tic_mode()" );
    const char *txt;
    switch ( get_tic_mode() )
    {
        case TIC_MODE_HISTORIQUE:
            txt = STATUS_HISTORIQUE;
            break;
        case TIC_MODE_STANDARD:
            txt = STATUS_STANDARD;
            break;
        default:
            txt = STATUS_INCONNU;
    }
    printf( FMT_TICMODE, txt );
}

void TicStatus::print_mqtt()
{
//    ESP_LOGD( TAG, "TicStatus::print_mqtt()" );
    char mqtt[32];
    get_mqtt_status ( mqtt, sizeof(mqtt) );
    printf ( FMT_MQTT, mqtt );
}

void TicStatus::print_wifi()
{
    wifi_ap_record_t ap_info;
    //const char *txt;

    esp_err_t err = esp_wifi_sta_get_ap_info ( &ap_info );
    if( err == ESP_OK )
    {
        printf ( FMT_WIFI, ap_info.ssid, ap_info.primary, ap_info.rssi );
    }
    else if ( err == ESP_ERR_WIFI_NOT_CONNECT )
    {
        printf ( FMT_WIFI_NOCNX );
    }
    else  //if ( err == ESP_ERR_WIFI_CONN )
    {
        printf ( FMT_WIFI_ERROR );
    }
}


void TicStatus::print_ip_addr()
{
    char ip[32];
    char gw[32];
    char netmask[32];

    esp_netif_ip_info_t ip_info;
    get_ip_info( &ip_info );

    snprintf( ip, sizeof(ip), IPSTR, IP2STR( &(ip_info.ip) )) ;
    snprintf( gw, sizeof(gw), IPSTR, IP2STR( &(ip_info.gw) )) ;
    snprintf( netmask, sizeof(netmask), IPSTR, IP2STR( &(ip_info.netmask) )) ;
    
    printf( FMT_IP_INFO,  ip, gw, netmask);
}


void TicStatus::print_status()
{
//    ESP_LOGD( TAG, "TicStatus::print_status()" );
    print_time();
    print_uart();
    print_tic_mode();
    print_wifi();
    print_ip_addr();
    print_mqtt();
}


extern "C" tic_error_t status_print()
{
//    ESP_LOGD( TAG, "status_print()" );
    if ( !s_tic_status )
    {
        ESP_LOGE( TAG, "init required with status_init()");
        return  TIC_ERR_NOT_INITIALIZED;
    }
    s_tic_status->print_status();
    return TIC_OK;
}


extern "C" tic_error_t status_init()
{
    ESP_LOGD( TAG, "status_init()" );
    assert( s_tic_status == NULL );    // already initialized ?

    s_tic_status = new TicStatus();
    if ( !s_tic_status )
    {
        ESP_LOGE( TAG, "new TicStatus() failed" );
        return TIC_ERR_APP_INIT;
    }
    s_tic_status->setup();
    return TIC_OK;
}

#endif   // CONFIG_TIC_CONSOLE
