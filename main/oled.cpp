
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

#include "esp_log.h"

#include "lcdgfx.h"
#include "lcdgfx_gui.h"

#include "tic_types.h"
#include "tic_config.h"
#include "status.h"
#include "oled.h"
#include "status.h"


static const char *TAG = "oled.cpp";

const SPlatformI2cConfig tic_default_display_config = { 
    .busId = -1,
    .addr = OLED_I2C_ADDRESS,
    .scl = OLED_GPIO_SCL,
    .sda = OLED_GPIO_SDA,
    .frequency = 0 
};

const char *LABEL_UART = "UART";
const char *LABEL_TIC  = " TIC";
const char *LABEL_WIFI  = "Wifi";
const char *LABEL_IP_ADDR  = "  IP";
const char *LABEL_MQTT_STATUS  = "MQTT";
const char *LABEL_CLOCK = "Time";
const char *LABEL_PAPP  = "PAPP";
const char *LABEL_MESSAGE  = "MSG:";


static const char *STATUS_TIC_TXT_NOSIGNAL   = "no signal";
static const char *STATUS_TIC_TXT_HISTORIQUE = "historique";
static const char *STATUS_TIC_TXT_STANDARD   = "standard";
//static const char *STATUS_TIC_TXT_NODATA = "no data";
static const char *STATUS_CONNECTING = "connecting...";
static const char *STATUS_CONNECTED  = "connected";
//static const char *STATUS_ERROR = "error";


#define LINE_BUF_SIZE 32
#define FONT_HEIGHT 8
#define FONT_WIDTH  6


// todo Transformer en classe
#define DISPLAY_EVENT_DATA_SIZE 32

typedef struct display_event_s {
    display_event_type_t info;
    char txt[DISPLAY_EVENT_DATA_SIZE];
} display_event_t;


class DisplayLine 
{
private:
    const char *m_label;
    uint8_t m_position;
    uint8_t m_length;
    char m_newline[LINE_BUF_SIZE];
    char m_oldline[LINE_BUF_SIZE];

public:
    DisplayLine( const char *label, uint8_t position, uint8_t length );
    uint8_t get_position() { return m_position; }
    void set_info( const char *info);
    const char * get_txt();
};


DisplayLine::DisplayLine( const char *label, uint8_t position, uint8_t length )
    : m_label(label)
    , m_position(position)
    , m_length(length)
{ 
    //ESP_LOGD( TAG, "DisplayLine::DisplayLine(%s,%d,%d)", label, position, length );
    assert( length < LINE_BUF_SIZE );
    m_newline[0] = '\0';
    m_oldline[0] = '\0';
}


void DisplayLine::set_info( const char *info )
{
    snprintf( m_newline, sizeof(m_newline)-1, "%s %s                           ", m_label, info);
    m_newline[m_length] = '\0';
}


const char* DisplayLine::get_txt()
{
    if( strncmp( m_oldline, m_newline, m_length ) == 0 )
    {
        return NULL;
    }
    strncpy( m_oldline, m_newline, sizeof(m_oldline) );
    return m_oldline;
}


class TicDisplay : public DisplaySSD1306_128x64_I2C
{
private:
    uint8_t m_font_width;
    uint8_t m_font_height;

    // queue pour découpler 
    //    la réception des events -> tache event_loop lancee dans status.c
    //    le traitement des events par la librairie lcdgfx -> tâche oled_task
    QueueHandle_t m_queue;

    // pour affichage combiné baudrate/mode sur une seule ligne
    tic_mode_t m_mode;
    int m_baudrate;      // 0:inconnu;   1200:historique;   9600:standard

    DisplayLine *m_lines[DISPLAY_EVENT_TYPE_MAX];

    void reset_data();
    void refresh();
    int line_length();

    // handler appelé par l'event loop ESP
    static void status_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data );
    
    // reception des evènements entrants
    void event_handler( esp_event_base_t event_base, int32_t event_id, void *event_data );
    void update_tic_et_baudrate();
    void event_baudrate (int baudrate);
    void event_tic_mode (tic_mode_t mode);
    void event_clock_tick (const char *time_str);
    void event_puissance (int puissance);
    void event_wifi (const char *ssid);
    void event_mqtt (const char* status);
    tic_error_t oled_update( display_event_type_t type, const char* txt );

public: 
    explicit TicDisplay( int8_t rstPin, const SPlatformI2cConfig &config  );
    tic_error_t setup();
    void loop();
};


TicDisplay::TicDisplay( int8_t rstPin, const SPlatformI2cConfig &config )
    : DisplaySSD1306_128x64_I2C( rstPin, config )
    , m_font_width(FONT_WIDTH)
    , m_font_height(FONT_HEIGHT)
    , m_queue(NULL)
    , m_mode(TIC_MODE_INCONNU)
    , m_baudrate(0)               // 0:inconnu;   1200:historique;   9600:standard
{
    const uint32_t w = 128 / m_font_width;
    uint8_t i = 0;
    m_lines[DISPLAY_CLOCK] = new DisplayLine( LABEL_CLOCK, i++, w );
    m_lines[DISPLAY_TIC_STATUS] = new DisplayLine( LABEL_TIC, i++, w );
    m_lines[DISPLAY_WIFI_STATUS] = new DisplayLine( LABEL_WIFI, i++, w);
    m_lines[DISPLAY_IP_ADDR] = new DisplayLine( LABEL_IP_ADDR, i++, w );
    m_lines[DISPLAY_MQTT_STATUS] = new DisplayLine( LABEL_MQTT_STATUS, i++, w );
    m_lines[DISPLAY_PAPP] = new DisplayLine( LABEL_PAPP, i++, w );
    m_lines[DISPLAY_MESSAGE] = new DisplayLine( LABEL_MESSAGE, i++, w  );

    assert(i==DISPLAY_EVENT_TYPE_MAX);    //  nombre de DisplayLines incorrect
}


void TicDisplay::reset_data()
{
    int i;
    for( i=0; i<DISPLAY_EVENT_TYPE_MAX; i++ )
    {
        m_lines[i]->set_info("");
    }
}


int  TicDisplay::line_length()
{
    return width() / m_font_width;
}

void TicDisplay::refresh()
{
    setFixedFont( ssd1306xled_font6x8 );

    int n;
    char buf[LINE_BUF_SIZE];
    const char *txt;
    DisplayLine *line;
    for( n=0; n<DISPLAY_EVENT_TYPE_MAX; n++ )
    {
        line = m_lines[n];
        txt = line->get_txt();   // returns NULL if no refresh is needed
        if( txt != NULL )
        {
            strncpy( buf, txt, sizeof(buf) );   // pour tronquer à la longueur max
            printFixed(0, ( m_font_height * line->get_position() ), buf, STYLE_NORMAL );
            ESP_LOGD( TAG, "refresh line %" PRIu8 " '%s'", line->get_position(), txt );
        }
    }
}


tic_error_t TicDisplay::setup()
{
    m_queue = xQueueCreate( 20, sizeof( display_event_t ) );
    if( m_queue == NULL )
    {
        ESP_LOGE( TAG, "xCreateQueue() failed" );
        return TIC_ERR_APP_INIT;
    }

    // handler pour les STATUS_EVENT
    tic_error_t tic_err = status_register_event_handler( ESP_EVENT_ANY_ID, &status_event_handler, this );
    if ( tic_err != TIC_OK)
    {
        return TIC_ERR_APP_INIT;   // message loggué par status_register_event_handler()
    }

    // Select the font to use with menu and all font functions
    setFixedFont( ssd1306xled_font6x8 );
    begin();

    /* Uncomment 2 lines below to rotate your ssd1306 display by 180 degrees. */
    // getInterface().flipVertical();
    // getInterface().flipHorizontal();

    clear();
    printFixed(0, height()/3 , "Init ...", STYLE_NORMAL);
    reset_data();

    return TIC_OK;
}


void TicDisplay::loop()
{
    display_event_t event;
    clear();
    for(;;)
    {
        BaseType_t evt_received = xQueueReceive( m_queue, &event, 10000 / portTICK_PERIOD_MS );
        if( evt_received == pdTRUE )
        {
            //ESP_LOGD( TAG, "Oled event %#x '%s' %p", event.info, event.txt, queue );
            m_lines[ event.info ]->set_info( event.txt );
        }
        refresh();
    }
}


 // oled_update est appelé depuis les handlers de STATUS_EVENTS
 // depose un messages dans la queue
tic_error_t TicDisplay::oled_update( display_event_type_t type, const char* txt )
{
    assert(m_queue);

    display_event_t evt;
    evt.info = type;

    strncpy( evt.txt, txt, sizeof( evt.txt ) );
    if( xQueueSend( m_queue, &evt, 0 ) != pdTRUE ) 
    {
        ESP_LOGI( TAG, "oled_update() echec. Probablement queue pleine. Type %#x '%s'", evt.info, evt.txt );
        return TIC_ERR;
    } 
    return TIC_OK;
}


void TicDisplay::update_tic_et_baudrate()
{
    const char *msg = NULL;
    char buf[DISPLAY_EVENT_DATA_SIZE];
    if (m_mode == TIC_MODE_HISTORIQUE)
    {
        msg = STATUS_TIC_TXT_HISTORIQUE;
    } 
    else if (m_mode == TIC_MODE_STANDARD)
    {
        msg = STATUS_TIC_TXT_STANDARD;
    }
    else if (m_mode == TIC_MODE_INCONNU && m_baudrate>0)
    {
        snprintf (buf, sizeof(buf), "%d baud", m_baudrate);
        msg = buf;
    }
    else
    {
        msg = STATUS_TIC_TXT_NOSIGNAL;
    }
    oled_update( DISPLAY_TIC_STATUS, msg );
}


void TicDisplay::event_baudrate (int baudrate)
{
    ESP_LOGD( TAG, "STATUS_EVENT_BAUDRATE baudrate=%d", baudrate);
    m_baudrate = baudrate;
    update_tic_et_baudrate();

}

void TicDisplay::event_tic_mode (tic_mode_t mode )
{
    ESP_LOGD( TAG, "STATUS_EVENT_TIC_MODE mode=%#02x", mode);
    m_mode = mode;
    update_tic_et_baudrate();
}

void TicDisplay::event_clock_tick (const char *time_str)
{
    ESP_LOGD( TAG, "STATUS_EVENT_CLOCK_TICK %s", time_str);
    oled_update( DISPLAY_CLOCK, time_str);
}

void TicDisplay::event_puissance (int puissance)
{
    ESP_LOGD( TAG, "STATUS_EVENT_PUISSANCE %d", puissance);
    char buf[16];
    snprintf( buf, sizeof(buf), "%d W", puissance );
    oled_update( DISPLAY_PAPP, buf );
}

void TicDisplay::event_wifi (const char *ssid)
{
    ESP_LOGD( TAG, "STATUS_EVENT_WIFI '%s'", (ssid[0] ? ssid : STATUS_CONNECTING) );
    const char *txt = ( ssid == NULL ) ? STATUS_CONNECTED : ssid;
    oled_update( DISPLAY_WIFI_STATUS, txt );
}

void TicDisplay::event_mqtt( const char* status )
{
    ESP_LOGD( TAG, "STATUS_EVENT_MQTT %s", status);
    oled_update( DISPLAY_MQTT_STATUS, status );
}


/*

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    //ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    //esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_BEFORE_CONNECT:
    case MQTT_EVENT_DISCONNECTED:
        oled_update( DISPLAY_MQTT_STATUS, STATUS_CONNECTING );
        break;
    case MQTT_EVENT_CONNECTED:
        oled_update( DISPLAY_MQTT_STATUS, STATUS_CONNECTED );
        break;
    case MQTT_EVENT_ERROR:
        oled_update( DISPLAY_MQTT_STATUS, STATUS_ERROR );
        break;
    }
}
*/


/*
void status_wifi_sta_connecting()
{
    oled_update( DISPLAY_WIFI_STATUS, STATUS_CONNECTING );

    // disbale upper layers 
    //status_wifi_lost_ip();   // clear oled_ip et oled_mqtt
}


void status_wifi_sta_connected( const char *ssid )
{
    const char *txt = ( ssid == NULL ) ? STATUS_CONNECTED : ssid;
    oled_update( DISPLAY_WIFI_STATUS, txt );
    //status_wifi_lost_ip();   // clear oled_ip et oled_mqtt
}


void status_wifi_got_ip( esp_netif_ip_info_t *ip_info )
{
    char buf[32];
    snprintf( buf, sizeof(buf), IPSTR, IP2STR( &(ip_info->ip) ) );
    oled_update( DISPLAY_IP_ADDR, buf );
    // status_mqtt_disconnected();
}

void status_wifi_lost_ip()
{
    oled_update( DISPLAY_IP_ADDR, "--" );
    // status_mqtt_disconnected(); 
}
*/



void TicDisplay::event_handler( esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    assert ( event_base==STATUS_EVENTS);
    switch( event_id )
    {
        case STATUS_EVENT_BAUDRATE:
            event_baudrate (*(int*)event_data);
            break;
        case STATUS_EVENT_TIC_MODE:
            event_tic_mode (*(tic_mode_t *)event_data);
            break;
        case STATUS_EVENT_CLOCK_TICK:
            event_clock_tick ((const char *)event_data);
            break;
        case STATUS_EVENT_PUISSANCE :
            event_puissance (*(int *)event_data);
            break;
        case STATUS_EVENT_WIFI:
            event_wifi ((const char *)event_data);
            break;
        case STATUS_EVENT_MQTT:
            event_mqtt ((const char *)event_data);
            break;
        case STATUS_EVENT_NONE:
        default:
            ESP_LOGW( TAG, "STATUS_EVENT_NONE ou invalide");
    }
}

// custom status event loop
void TicDisplay::status_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    // handler_arg doit être un pointeur vers TicDisplay
    TicDisplay *display = (TicDisplay *)arg;
    display->event_handler (event_base, event_id, event_data);
}


static void oled_task(void *pvParams)
{
    TicDisplay display( OLED_GPIO_RST, tic_default_display_config );
    display.setup();
    display.loop();
}


extern "C" tic_error_t oled_task_start( )
{
    ESP_LOGD( TAG, "oled_task_start()" );
    if( xTaskCreate( oled_task, "oled_task", 8192, NULL, 2, NULL ) != pdPASS )
    { 
        ESP_LOGE( TAG, "xTaskCreate() failed" );
        return TIC_ERR_APP_INIT;
    }
    return TIC_OK;
}


