
#include <stdint.h>
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

//static const char *TAG = "status.c";

const char *STATUS_TIC_TXT_NOSIGNAL   = "no signal";
const char *STATUS_TIC_TXT_HISTORIQUE = "historique";
const char *STATUS_TIC_TXT_STANDARD   = "standard";
const char *STATUS_TIC_TXT_NODATA = "no data";

const char *STATUS_CONNECTING = "connecting...";
const char *STATUS_CONNECTED  = "connected";
const char *STATUS_ERROR = "error";

static TimerHandle_t s_wdt_uart;
static TimerHandle_t s_wdt_ticframe;
static EventGroupHandle_t s_to_ticled;
static QueueHandle_t s_to_oled;

// bits elementaire de statut
#define BIT_SIGNAL_HISTORIQUE  BIT0
#define BIT_SIGNAL_STANDARD    BIT1
#define BIT_TELEINFO_DATA      BIT2
static EventGroupHandle_t s_status_bits;


void update_status_tic()
{
    const char *msg = NULL;    
    EventBits_t status = xEventGroupGetBits( s_status_bits );
    switch( status & (BIT_SIGNAL_HISTORIQUE | BIT_SIGNAL_STANDARD) )
    {
        case 0:
            msg = STATUS_TIC_TXT_NOSIGNAL;
            break;
        case BIT_SIGNAL_HISTORIQUE:
            msg = (status & BIT_TELEINFO_DATA) ? STATUS_TIC_TXT_HISTORIQUE : STATUS_TIC_TXT_NODATA;
            break;
        case BIT_SIGNAL_STANDARD:
            msg = (status & BIT_TELEINFO_DATA) ? STATUS_TIC_TXT_STANDARD : STATUS_TIC_TXT_NODATA;
            break;
        default:
            msg = STATUS_ERROR;   // cannot have both modes 
    }
    oled_update( s_to_oled, DISPLAY_TIC_STATUS, msg );
}


static void tic_timeout()
{
    xEventGroupClearBits( s_status_bits, BIT_TELEINFO_DATA );
    update_status_tic();
}


static void uart_timeout()
{
    xEventGroupClearBits( s_status_bits, (BIT_SIGNAL_HISTORIQUE|BIT_SIGNAL_STANDARD) );
    tic_timeout();
    //update_status_tic();     called in tic_timeout()
}


void status_rcv_uart( tic_mode_t mode, TickType_t next_before )
{
    // reset uart watchdog
    if( next_before > 0 )
    {
        xTimerChangePeriod( s_wdt_uart, next_before, 1 );
    }
    xTimerReset( s_wdt_uart, 0);

    ticled_blink_short( s_to_ticled );

    switch( mode )
    {
        case TIC_MODE_HISTORIQUE:
            xEventGroupSetBits( s_status_bits, BIT_SIGNAL_HISTORIQUE );
            break;
        case TIC_MODE_STANDARD:
            xEventGroupSetBits( s_status_bits, BIT_SIGNAL_STANDARD );
            break;
        default:
            // seeting both bits is error an condition
            xEventGroupSetBits( s_status_bits, (BIT_SIGNAL_HISTORIQUE|BIT_SIGNAL_STANDARD) );
    }
    update_status_tic();
}

void status_rcv_tic_frame( TickType_t next_before)
{
    // reset tic watchdog
    if( next_before > 0 )
    {
        xTimerChangePeriod( s_wdt_ticframe, next_before, 1 );
    }
    xTimerReset( s_wdt_ticframe, 0 );

    ticled_blink_long( s_to_ticled );
    xEventGroupSetBits( s_status_bits, BIT_TELEINFO_DATA );
    update_status_tic();
}


void status_wifi_sta_connecting()
{
    oled_update( s_to_oled, DISPLAY_WIFI_STATUS, STATUS_CONNECTING );

    // disbale upper layers 
    //status_wifi_lost_ip();   // clear oled_ip et oled_mqtt
}


void status_wifi_sta_connected( const char *ssid )
{
    const char *txt = ( ssid == NULL ) ? STATUS_CONNECTED : ssid;
    oled_update( s_to_oled, DISPLAY_WIFI_STATUS, txt );
    //status_wifi_lost_ip();   // clear oled_ip et oled_mqtt
}


void status_wifi_got_ip( esp_netif_ip_info_t *ip_info )
{
    char buf[32];
    snprintf( buf, sizeof(buf), IPSTR, IP2STR( &(ip_info->ip) ) );
    oled_update( s_to_oled, DISPLAY_IP_ADDR, buf );
    // status_mqtt_disconnected();
}

void status_wifi_lost_ip()
{
    oled_update( s_to_oled, DISPLAY_IP_ADDR, "--" );
    // status_mqtt_disconnected(); 
}


void status_mqtt_update( const char *status )
{
    oled_update( s_to_oled, DISPLAY_MQTT_STATUS, status );
}


void status_clock_update( const char* time_str)
{
    oled_update( s_to_oled, DISPLAY_CLOCK, time_str);
}


void status_papp_update( uint32_t papp )
{
    char buf[16];
    snprintf( buf, sizeof(buf), "%lu W", papp );
    oled_update( s_to_oled, DISPLAY_PAPP, buf );
}


void status_init( QueueHandle_t to_oled, EventGroupHandle_t to_ticled )
{
    s_to_oled = to_oled;
    s_to_ticled = to_ticled;

    s_status_bits = xEventGroupCreate();

    // timers d'expiration du signal serie et du decodeur TIC
    s_wdt_uart = xTimerCreate( "uart_timer", 
                                STATUS_UART_DEFAULT_TIMEOUT / portTICK_PERIOD_MS, 
                                pdFALSE, 
                                NULL, 
                                uart_timeout );
    xTimerStart( s_wdt_uart, 1 );

    s_wdt_ticframe = xTimerCreate( "ticframe_timer", 
                                    STATUS_TICFRAME_DEFAUT_TIMEOUT / portTICK_PERIOD_MS,
                                    pdFALSE,
                                    NULL,
                                    uart_timeout );
    xTimerStart( s_wdt_ticframe, 1 );
}
