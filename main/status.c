
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"

#include "esp_netif.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "status.h"
#include "oled.h"
#include "ticled.h"

static const char *TAG = "status.c";

const char *STATUS_TIC_TXT_NOSIGNAL   = "no signal";
const char *STATUS_TIC_TXT_HISTORIQUE = "historique";
const char *STATUS_TIC_TXT_STANDARD   = "standard";
const char *STATUS_TIC_TXT_NODATA = "no data";

const char *STATUS_CONNECTING = "connecting...";
const char *STATUS_CONNECTED  = "connected";
const char *STATUS_ERROR = "error";

static TimerHandle_t s_wdt_baudrate = NULL;
static TimerHandle_t s_wdt_ticmode = NULL;

// Status event definitions 
ESP_EVENT_DEFINE_BASE(STATUS_EVENTS);

static esp_event_loop_handle_t s_status_evt_loop = NULL;

static tic_error_t post_integer (int value, int32_t evt_id)
{
    esp_err_t err = esp_event_post_to(s_status_evt_loop, 
            STATUS_EVENTS, evt_id, 
            &value, sizeof(value), 
            portMAX_DELAY);
    if (err != ESP_OK)
    {
        ESP_LOGE( TAG, "esp_event_post_to( <int> ) erreur %#02x", err);
        return TIC_ERR;
    }
    return TIC_OK;
}

static tic_error_t post_string (const char *str, int32_t evt_id)
{
    esp_err_t err = esp_event_post_to (s_status_evt_loop,
            STATUS_EVENTS, evt_id, 
            str, strlen(str)+1, 
            portMAX_DELAY);
    if (err != ESP_OK)
    {
        ESP_LOGE( TAG, "esp_event_post_to( <string> ) erreur %#02x", err);
        return TIC_ERR;
    }
    return TIC_OK;
}

static void tic_mode_timeout()
{
    ESP_LOGD (TAG, "watchdog status tic_mode expiré");
    post_integer ( TIC_MODE_INCONNU, STATUS_EVENT_TIC_MODE);   // ignore erreur
}

static void baudrate_timeout()
{
    ESP_LOGD (TAG, "watchdog status baudrate expiré");
    post_integer (0, STATUS_EVENT_BAUDRATE);            // ignore erreur
}

static void reset_watchdog ( TimerHandle_t wdt, TickType_t next_before)
{
    if ( wdt != NULL )
    {
        if( next_before > 0 )
        {
            xTimerChangePeriod( wdt, next_before, 1 ); 
        }
        xTimerReset( wdt, 10);
    }
}


tic_error_t status_update_baudrate (int baudrate, TickType_t next_before)
{
    ESP_LOGD (TAG, "status_update_baudrate(%d)", baudrate);
    reset_watchdog (s_wdt_baudrate, next_before);
    return post_integer( baudrate, STATUS_EVENT_BAUDRATE );
}

tic_error_t status_update_tic_mode( tic_mode_t mode, TickType_t next_before)
{
    ESP_LOGD (TAG, "status_update_tic_mode(%d)", mode);
    reset_watchdog (s_wdt_ticmode, next_before);
    return post_integer ((int)mode, STATUS_EVENT_TIC_MODE);
}

tic_error_t status_update_wifi (const char* ssid)
{
    ESP_LOGD (TAG, "status_update_wifi() ssid='%s'", ssid);
    return post_string( ssid, STATUS_EVENT_WIFI);
}

tic_error_t status_update_mqtt (const char *mqtt_status)
{
    ESP_LOGD (TAG, "status_update_mqtt(%s)", mqtt_status);
    return post_string (mqtt_status, STATUS_EVENT_MQTT);
}

tic_error_t status_update_clock( const char* time_str)
{
    ESP_LOGD (TAG, "status_update_mqtt(%s)", time_str);
    return post_string (time_str, STATUS_EVENT_CLOCK_TICK);
}

tic_error_t status_update_puissance( uint32_t puissance )
{
    ESP_LOGD (TAG, "status_update_puissance( %"PRIu32" )", puissance);
    return post_integer (puissance, STATUS_EVENT_PUISSANCE);
}


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






// *********** A DEPLACER ****************
static struct {
    tic_mode_t mode;
    int baudrate;      // 0:inconnu;   1200:historique;   9600:standard
} s_tic = { 
    .mode = TIC_MODE_INCONNU, 
    .baudrate = 0
};


void update_oled_ticmode()
{
    const char *msg = NULL;
    char buf[DISPLAY_EVENT_DATA_SIZE];
    if (s_tic.mode == TIC_MODE_HISTORIQUE)
    {
        msg = STATUS_TIC_TXT_HISTORIQUE;
    } 
    else if (s_tic.mode == TIC_MODE_STANDARD)
    {
        msg = STATUS_TIC_TXT_STANDARD;
    }
    else if (s_tic.mode == TIC_MODE_INCONNU && s_tic.baudrate>0)
    {
        snprintf (buf, sizeof(buf), "%d baud", s_tic.baudrate);
        msg = buf;
    }
    else
    {
        msg = STATUS_TIC_TXT_NOSIGNAL;
    }
    oled_update( DISPLAY_TIC_STATUS, msg );
}

void event_baudrate (int baudrate)
{
    ESP_LOGD( TAG, "STATUS_EVENT_UART_RCV baudrate %d", baudrate);

    //*********TODO***********
    //   creer un handler dans TICLED
    ticled_blink_short();

    //*********TODO***********
    // creer un handler dans OLED
    s_tic.baudrate = baudrate;
    update_oled_ticmode();

    //*********TODO***********
    // remplacer par une API directe entre uart.c et decode.c
    // ou par un event handler dans decode.c
    //decode_set_mode(mode);
}

void event_tic_mode (tic_mode_t mode )
{
    switch(mode)
    {
        case TIC_MODE_HISTORIQUE:
            ESP_LOGD( TAG, "STATUS_EVENT_DECODE_FRAME mode historique");
            break;
        case TIC_MODE_STANDARD:
            ESP_LOGD( TAG, "STATUS_EVENT_DECODE_FRAME mode standard");
            break;
        case TIC_MODE_INCONNU:
        default:
            ESP_LOGD( TAG, "STATUS_EVENT_DECODE_FRAME mode inconnu");
    }

    //*********TODO***********
    //   creer un handler dans TICLED
    ticled_blink_long();

    //*********TODO***********
    // creer un handler dans OLED
    s_tic.mode = mode;
    update_oled_ticmode();
}

static void event_clock_tick (const char *time_str)
{
    ESP_LOGD( TAG, "STATUS_EVENT_CLOCK_TICK %s", time_str);

    //*********TODO***********
    oled_update( DISPLAY_CLOCK, time_str);
}


static void event_puissance (int puissance)
{
    ESP_LOGD( TAG, "STATUS_EVENT_PUISSANCE %d", puissance);
    //*********TODO***********
    char buf[16];
    snprintf( buf, sizeof(buf), "%d W", puissance );
    oled_update( DISPLAY_PAPP, buf );
}


static void event_wifi (const char *ssid)
{
    ESP_LOGD( TAG, "STATUS_EVENT_WIFI '%s'", (ssid[0] ? ssid : STATUS_CONNECTING) );
}


static void event_mqtt( const char* mqtt_status )
{
    ESP_LOGD( TAG, "STATUS_EVENT_MQTT %s", mqtt_status);
}


// custom status event loop
void status_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
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


// default ESP event loop
static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;

    switch( event_id ) 
    {
    case IP_EVENT_STA_GOT_IP:               /*!< station got IP from connected AP */
        
        esp_netif_ip_info_t *ip_info = &event->ip_info;
        char buf[32];
        snprintf( buf, sizeof(buf), IPSTR, IP2STR( &(ip_info->ip) ) );
        oled_update( DISPLAY_IP_ADDR, buf );
        ESP_LOGI(TAG, "Got IP %s", buf );
        break;

    case IP_EVENT_STA_LOST_IP:              /*!< station lost IP and the IP is reset to 0 */
        oled_update( DISPLAY_IP_ADDR, "" );
        ESP_LOGI(TAG, "Lost IP address");
        break;

    default:
        ESP_LOGD( TAG, "IP_EVENT id=%#lx", event_id );
    }
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
void status_mqtt_update( const char *status )
{
    oled_update( DISPLAY_MQTT_STATUS, status );
}
*/

// enregistre un handler pour les STATUS_EVENT
tic_error_t status_register_event_handler (esp_event_handler_t handler_func, int32_t evt_id )
{
    esp_err_t err = esp_event_handler_instance_register_with( s_status_evt_loop, 
                                                            STATUS_EVENTS,
                                                            evt_id,
                                                            handler_func,
                                                            NULL,
                                                            NULL);
    if (err!=ESP_OK)
    {
        ESP_LOGD (TAG, "status_register_event_handler() erreur %#02x", err);
        return TIC_ERR;
    }
    return TIC_OK;
}



tic_error_t status_init()
{
    esp_err_t esp_err;

    esp_event_loop_args_t loop_args = {
        .queue_size = 5,
        .task_name = "status_evt_loop_task", // task will be created
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = 3072,
        //.task_core_id = tskNO_AFFINITY
    };

    // Create the event loop
    esp_err = esp_event_loop_create(&loop_args, &s_status_evt_loop);
    if ( esp_err != ESP_OK)
    {
        ESP_LOGE (TAG, "esp_event_loop_create() erreur %#02x", esp_err);
        return TIC_ERR_APP_INIT;
    }

    // handler pour les STATUS_EVENT
    tic_error_t tic_err = status_register_event_handler( &status_event_handler, ESP_EVENT_ANY_ID );
    if ( tic_err != TIC_OK)
    {
        return TIC_ERR_APP_INIT;   // message loggué par status_register_event_handler()
    }

    // handler pour l'acquisition/perte d'adresse IP s'enregistre sur l'event loop par défaut
    // TODO ************* deplacer dans OLED ************
    esp_err = esp_event_handler_instance_register(IP_EVENT,
                                                         ESP_EVENT_ANY_ID, //IP_EVENT_STA_GOT_IP,
                                                         &ip_event_handler,
                                                         NULL,
                                                         NULL );
    if ( esp_err != ESP_OK)
    {
        ESP_LOGE (TAG, "esp_event_handler_instance_register() erreur %#02x", esp_err);
        return TIC_ERR_APP_INIT;
    }

    // timers d'expiration du signal serie et du decodeur TIC
    s_wdt_baudrate = xTimerCreate( "uart_timer", 
                                STATUS_BAUDRATE_DEFAULT_TIMEOUT / portTICK_PERIOD_MS, 
                                pdFALSE, 
                                NULL, 
                                baudrate_timeout );
                                
    s_wdt_ticmode = xTimerCreate( "decode_status_timer", 
                                    STATUS_TICMODE_DEFAUT_TIMEOUT / portTICK_PERIOD_MS,
                                    pdFALSE,
                                    NULL,
                                    tic_mode_timeout );

    if (!s_wdt_baudrate || !s_wdt_ticmode)
    {
        ESP_LOGE (TAG, "xTimerCreate() failed");
        return TIC_ERR_APP_INIT;
    }
    xTimerStart( s_wdt_baudrate, 10 );
    xTimerStart( s_wdt_ticmode, 10 );
    return pdTRUE;
}
