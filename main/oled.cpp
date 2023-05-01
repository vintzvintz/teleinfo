
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
#include "oled.h"


#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif


static const char *TAG = "oled_task";


static QueueHandle_t s_to_oled = NULL;

/*** 
 * oled_update est appelé depuis les autres tâches pour envoyer des infos à l'afficheur
 */
extern "C"
tic_error_t oled_update( display_event_type_t type, const char* txt )
{
    if( s_to_oled == NULL )
    {
        ESP_LOGD( TAG, "oled pas encore initialisé" );
        return TIC_ERR;
    }

    display_event_t evt;
    evt.info = type;

    strncpy( evt.txt, txt, sizeof( evt.txt ) );
    if( xQueueSend( s_to_oled, &evt, 0 ) != pdTRUE ) 
    {
        ESP_LOGI( TAG, "oled_update() echec. Probablement queue pleine. Type %#x '%s'", evt.info, evt.txt );
        return TIC_ERR;
    } 
    return TIC_OK;
}

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

#define LINE_BUF_SIZE 32
#define FONT_HEIGHT 8
#define FONT_WIDTH  6


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
    DisplayLine *m_lines[DISPLAY_EVENT_TYPE_MAX];

    void reset_data();
    void refresh();
    int line_length();

public: 
    explicit TicDisplay( int8_t rstPin, const SPlatformI2cConfig &config  );
    void setup();
    void loop( QueueHandle_t queue );
};


TicDisplay::TicDisplay( int8_t rstPin, const SPlatformI2cConfig &config )
    : DisplaySSD1306_128x64_I2C( rstPin, config )
    , m_font_width(FONT_WIDTH)
    , m_font_height(FONT_HEIGHT)
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
            strncpy( buf, txt, sizeof(buf) );
            printFixed(0, ( m_font_height * line->get_position() ), buf, STYLE_NORMAL );
            ESP_LOGD( TAG, "%s", txt );
        }
    }
}


void TicDisplay::setup()
{
    // Select the font to use with menu and all font functions
    setFixedFont( ssd1306xled_font6x8 );
    begin();

    /* Uncomment 2 lines below to rotate your ssd1306 display by 180 degrees. */
    // getInterface().flipVertical();
    // getInterface().flipHorizontal();

    clear();
    printFixed(0, height()/3 , "Init ...", STYLE_NORMAL);
    reset_data();
}


void TicDisplay::loop( QueueHandle_t queue )
{
    display_event_t event;
    clear();
    for(;;)
    {
        BaseType_t evt_received = xQueueReceive( queue, &event, 10000 / portTICK_PERIOD_MS );
        if( evt_received == pdTRUE )
        {
            //ESP_LOGD( TAG, "Oled event %#x '%s' %p", event.info, event.txt, queue );
            m_lines[ event.info ]->set_info( event.txt );
        }
        refresh();
    }
}


static void oled_task(void *pvParams)
{
    //oled_task_param_t *task_params = (oled_task_param_t *)pvParams;
    TicDisplay display( OLED_GPIO_RST, tic_default_display_config );
    display.setup();
    display.loop( s_to_oled );
}


extern "C" tic_error_t oled_task_start( )
{
    esp_log_level_set( TAG, ESP_LOG_INFO );
    ESP_LOGD( TAG, "oled_task_start()" );

    // Reception des infos à afficher sur l'écran OLED
    s_to_oled = xQueueCreate( 50, sizeof( display_event_t ) );
    if( s_to_oled == NULL )
    {
        ESP_LOGE( TAG, "xCreateQueue() failed" );
        return TIC_ERR_APP_INIT;
    }

    if( xTaskCreate( oled_task, "oled_task", 8192, NULL, 2, NULL ) != pdPASS )
    { 
        ESP_LOGE( TAG, "xTaskCreate() failed" );
        return TIC_ERR_APP_INIT;
    }
    return TIC_OK;
}

