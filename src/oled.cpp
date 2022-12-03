
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#include "esp_log.h"

#include "lcdgfx.h"
#include "lcdgfx_gui.h"

#include "inter_task.h"
#include "oled.h"


#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif


static const char *TAG = "oled_task";

/* oled_update est appelé depuis les autres tâche pour envoyer des infos à l'afficheur
*/
extern "C"
void oled_update( QueueHandle_t to_oled, display_event_type_t type, const char* txt )
{
    display_event_t evt = {
        .info = type,
        //.txt[0] = '\0'
    };
    strncpy( evt.txt, txt, sizeof( evt.txt ) );
    if( xQueueSend( to_oled, &evt, 0 ) != pdTRUE ) 
    {
        ESP_LOGI( TAG, "oled_update() a échoué, probablement queue pleine. Type %#x '%s'", evt.info, evt.txt );
        //return TIC_ERR_QUEUEFULL;
    }
//    return TIC_OK;
}


const SPlatformI2cConfig tic_default_display_config = { 
    .busId = -1,
    .addr = OLED_I2C_ADDRESS,
    .scl = OLED_GPIO_SCL,
    .sda = OLED_GPIO_SDA,
    .frequency = 0 };



typedef struct oled_task_param_s {
    QueueHandle_t to_oled;
} oled_task_param_t;


class TicDisplay : public DisplaySSD1306_128x64_I2C
{
private:

    char tic_mode[32];
    char wifi_ssid[32];
    char mqtt_broker[32];
    char tic_papp[8];
    char message[64];


    void set_test1();
    void process_event( const display_event_t &event );


public: 
    explicit TicDisplay( int8_t rstPin, const SPlatformI2cConfig &config  );
    void setup();
    void loop_demo();
    void loop_on_queue( QueueHandle_t queue );
    void reset_data();
    void refresh();
};


TicDisplay::TicDisplay( int8_t rstPin, const SPlatformI2cConfig &config ) :
                  DisplaySSD1306_128x64_I2C( rstPin, config )
{
    reset_data();
}


void TicDisplay::reset_data()
{
    tic_mode[0] = '\0';
    wifi_ssid[0] = '\0';
    mqtt_broker[0] = '\0';
    tic_papp[0] = '\0';
    message[0] = '\0';
}


void TicDisplay::set_test1()
{
    strncpy( tic_mode, "Weshmode", sizeof( tic_mode ) );
    strncpy( wifi_ssid, "WeshSSID", sizeof( wifi_ssid ) );
    strncpy( mqtt_broker, "WeshBroker", sizeof( mqtt_broker ) );
    strncpy( message, "Wesh le message de ta mere", sizeof( message ) );
}



#define LINE_HEIGHT 8

#define LINE0  (0 * LINE_HEIGHT)
#define LINE1  (1 * LINE_HEIGHT)
#define LINE2  (2 * LINE_HEIGHT)
#define LINE3  (3 * LINE_HEIGHT)
#define LINE4  (4 * LINE_HEIGHT)
#define LINE5  (5 * LINE_HEIGHT)
#define LINE6  (6 * LINE_HEIGHT)
#define LINE7  (7 * LINE_HEIGHT)
 

void TicDisplay::refresh()
{
    //ESP_LOGI( TAG, "TicDisplay::refresh()" );

    char buf[64];

    setFixedFont( ssd1306xled_font6x8 );
    clear();

    snprintf( buf, sizeof(buf), " TIC %s", tic_mode );
    printFixed(0, LINE1, buf, STYLE_NORMAL);

    snprintf( buf, sizeof(buf), "Wifi %s", wifi_ssid );
    printFixed(0, LINE2, buf, STYLE_NORMAL);

    snprintf( buf, sizeof(buf), "MQTT %s", mqtt_broker );
    printFixed(0, LINE3, buf, STYLE_NORMAL);

    snprintf( buf, sizeof(buf), "PAPP %s", tic_papp );
    printFixed(0, LINE4, buf, STYLE_NORMAL);

    printFixed(0, LINE5, message, STYLE_NORMAL);

}


void TicDisplay::setup()
{

    /* Select the font to use with menu and all font functions */
    setFixedFont( ssd1306xled_font6x8 );

    begin();

    /* Uncomment 2 lines below to rotate your ssd1306 display by 180 degrees. */
    // display.getInterface().flipVertical();
    // display.getInterface().flipHorizontal();

    clear();
    printFixed(0, LINE1, "Init ...", STYLE_NORMAL);
}


void TicDisplay::loop_demo()
{
    reset_data();
    refresh();
    vTaskDelay( 1000 / portTICK_PERIOD_MS );

    set_test1();
    refresh();
    vTaskDelay( 1000 / portTICK_PERIOD_MS );
}


void TicDisplay::process_event( const display_event_t &event )
{
    switch( event.info )
    {
        case DISPLAY_TIC_STATUS:
        strncpy( tic_mode, event.txt, sizeof(tic_mode) );
        break;

        case DISPLAY_WIFI_STATUS:
        strncpy( wifi_ssid, event.txt, sizeof(wifi_ssid) );
        break;

        case DISPLAY_MQTT_STATUS:
        ESP_LOGI( TAG, "Oled MQTT update '%s'", event.txt );
        strncpy( mqtt_broker, event.txt, sizeof(mqtt_broker) );
        break;

        case DISPLAY_PAPP:
        ESP_LOGI( TAG, "Oled PAPP update '%s'", event.txt );
        strncpy( tic_papp, event.txt, sizeof(tic_papp) );
        break;
        
        case DISPLAY_MESSAGE:
        strncpy( message, event.txt, sizeof(message) );
        break;

        default:
        ESP_LOGE( TAG, "type d'information %#x inconnu", event.info );
    }
}


void TicDisplay::loop_on_queue( QueueHandle_t queue )
{
    display_event_t event;
    for(;;)
    {
        BaseType_t evt_received = xQueueReceive( queue, &event, 5000 / portTICK_PERIOD_MS );
        if( evt_received == pdTRUE )
        {
            ESP_LOGD( TAG, "Oled event %#x '%s' %p", event.info, event.txt, queue );
            process_event( event );
        }
        else
        {
            ESP_LOGD( TAG, "Refresh oled after timeout" );
            // xQueueReceive timeout
        }
        refresh();
    }
}


static void oled_task(void *pvParams)
{
    oled_task_param_t *task_params = (oled_task_param_t *)pvParams;
    TicDisplay display( OLED_GPIO_RST, tic_default_display_config );
    display.setup();
    display.loop_on_queue( task_params->to_oled );
}


extern "C" void oled_task_start( QueueHandle_t to_oled )
{
    ESP_LOGI( TAG, "oled_task_start()" );


    oled_task_param_t *task_params = (oled_task_param_t *)malloc( sizeof(oled_task_param_t) );
    if( task_params == NULL )
    {
        ESP_LOGE( TAG, "malloc() failed" );
        return;
    }

    task_params->to_oled = to_oled;
    xTaskCreatePinnedToCore( oled_task, "oled_task", 8192, task_params, 1, NULL, ARDUINO_RUNNING_CORE );
}


