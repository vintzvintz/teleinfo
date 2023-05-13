
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

#include "esp_netif.h"      // pour les IP_EVENT
#include "esp_log.h"


#ifdef CONFIG_TIC_CONSOLE


#include "tic_types.h"
#include "event_loop.h"
#include "uart_events.h"
#include "status.h"

static const char *TAG = "status.cpp";
/*
const char *LABEL_UART = "UART";
const char *LABEL_TIC  = " TIC";
const char *LABEL_WIFI  = "Wifi";
const char *LABEL_IP_ADDR  = "  IP";
const char *LABEL_MQTT_STATUS  = "MQTT";
const char *LABEL_CLOCK = "Time";
const char *LABEL_PAPP  = "PAPP";
const char *LABEL_MESSAGE  = "MSG:";
*/

//static const char *STATUS_TIC_TXT_NOSIGNAL   = "no signal";
static const char *STATUS_TIC_TXT_HISTORIQUE = "historique";
static const char *STATUS_TIC_TXT_STANDARD   = "standard";
static const char *STATUS_TIC_TXT_INCONNU = "inconnu";
//static const char *STATUS_CONNECTING = "connecting...";
//static const char *STATUS_CONNECTED  = "connected";
//static const char *STATUS_ERROR = "error";



class TicStatus
{
private:

    // pour affichage combiné baudrate/mode sur une seule ligne
    tic_mode_t m_mode;
    int m_baudrate;      // 0:inconnu;   1200:historique;   9600:standard
    char *m_mqtt_status;

    // protection des acces aux variables membres
    portMUX_TYPE m_spinlock;
    tic_mode_t get_tic_mode();
    void set_tic_mode( tic_mode_t mode );
    int get_baudrate();
    void set_baudrate (int rate);
    tic_error_t get_mqtt_status( char *buf, size_t len );
    tic_error_t set_mqtt_status (const char *status);

    // handlers enregistrés sur les event loops ESP - ne peuvent pas être des fonctions membre
//    static void static_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data );
    static void static_status_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data );

    // reception des evènements entrants
    //void ip_event_handler( esp_event_base_t event_base, int32_t event_id, void *event_data );
    void status_event_handler( esp_event_base_t event_base, int32_t event_id, void *event_data );
    void event_baudrate (int baudrate);
    void event_tic_data (const tic_data_t *data);
    void event_mqtt (const char* status);


    // printers pour chaque element du status
    void print_tic_mode();
    void print_uart();
    void print_mqtt();


public: 
    explicit TicStatus( );
    ~TicStatus();
    tic_error_t setup();

    //void update_all();

    void print_status();
};

static TicStatus *s_tic_status = NULL;


TicStatus::TicStatus() :
      m_mode(TIC_MODE_INCONNU)
    , m_baudrate(0)               // 0:inconnu;   1200:historique;   9600:standard
    , m_mqtt_status(NULL)
    , m_spinlock()
{}

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
    //esp_err_t err2 = esp_event_handler_instance_register( IP_EVENT, ESP_EVENT_ANY_ID, &static_ip_event_handler, this, NULL );

    if ( err != TIC_OK ) // || err2 != ESP_OK )
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
    
    size_t len = strlen (status);
    char *new_buf = (char *)calloc (1, len);
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



void TicStatus::event_baudrate (int baudrate)
{
    ESP_LOGD( TAG, "STATUS_EVENT_BAUDRATE baudrate=%d", baudrate);
    set_baudrate ( baudrate );
}

void TicStatus::event_tic_data (const tic_data_t *data )
{
    ESP_LOGD( TAG, "STATUS_EVENT_TIC_MODE mode=%#02x", data->mode);
    set_tic_mode ( data->mode );
}

void TicStatus::event_mqtt( const char* status )
{
    ESP_LOGD( TAG, "STATUS_EVENT_MQTT %s", status);
    set_mqtt_status ( status );
}


void TicStatus::status_event_handler( esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    assert ( event_base==STATUS_EVENTS);
    switch( event_id )
    {
        case STATUS_EVENT_BAUDRATE:
            event_baudrate (*(int*)event_data);
            break;
        case STATUS_EVENT_TIC_DATA:
            event_tic_data ( (tic_data_t *)event_data );
            break;
/*        case STATUS_EVENT_CLOCK_TICK:
            event_clock_tick ((const char *)event_data);
            break;
        case STATUS_EVENT_PUISSANCE :
            event_puissance (*(int *)event_data);
            break;
        case STATUS_EVENT_WIFI:
            event_wifi ((const char *)event_data);
            break;
        */
        case STATUS_EVENT_MQTT:
            event_mqtt ((const char *)event_data);
            break;
        default:
            ESP_LOGW( TAG, "STATUS_EVENT id %" PRIi32 " inconnu", event_id);
    }
}

/*
// fonction statique appelée par les event loop
void TicDisplay::static_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    // handler_arg doit être un pointeur vers TicDisplay
    TicDisplay *display = (TicDisplay *)arg;
    display->ip_event_handler (event_base, event_id, event_data);
}
*/


// fonction statique appelée par les event loop
void TicStatus::static_status_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    // handler_arg doit être un pointeur vers TicStatus
    TicStatus *ts = (TicStatus *)arg;
    ts->status_event_handler (event_base, event_id, event_data);
}


static const char *FMT_UART            = "UART rx_rate=%d tic_signal=%d\n";
static const char *FMT_UART_NOSIGNAL   = "UART rx_rate=%d no signal\n";
static const char *FMT_TICMODE         = "TIC  mode %s\n";
static const char *FMT_MQTT            = "MQTT %s\n";
//static const char *FMT_TICMODE = 
//static const char *FMT_TICMODE = 

void TicStatus::print_uart()
{
//    ESP_LOGD( TAG, "TicStatus::print_uart()" );
    int rx = uart_get_rx_baudrate();       // baudrate configuré sur l'UART
    int signal = get_baudrate();           // 0 si pas de signal
    if( signal > 0 )
    {
        printf( FMT_UART, rx, signal );
    }
    else
    {
        printf( FMT_UART_NOSIGNAL, rx );
    }
}

void TicStatus::print_tic_mode()
{
//    ESP_LOGD( TAG, "TicStatus::print_tic_mode()" );
    int mode = get_tic_mode();
    switch ( mode )
    {
        case TIC_MODE_HISTORIQUE:
            printf( FMT_TICMODE, STATUS_TIC_TXT_HISTORIQUE );
            break;
        case TIC_MODE_STANDARD:
            printf( FMT_TICMODE, STATUS_TIC_TXT_STANDARD );
            break;
        default:
            printf( FMT_TICMODE, STATUS_TIC_TXT_INCONNU );
    }
}


void TicStatus::print_mqtt()
{
//    ESP_LOGD( TAG, "TicStatus::print_mqtt()" );
    char mqtt[32];
    get_mqtt_status ( mqtt, sizeof(mqtt) );
    printf ( FMT_MQTT, mqtt );
}


void TicStatus::print_status()
{
//    ESP_LOGD( TAG, "TicStatus::print_status()" );
    print_uart();
    print_tic_mode();
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
