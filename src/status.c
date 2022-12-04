
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


const char *STATUS_UART_TXT_NOSIGNAL   = "no signal";
const char *STATUS_UART_TXT_HISTORIQUE = "historique";
const char *STATUS_UART_TXT_STANDARD   = "standard";

const char *STATUS_TIC_TXT_OK     = "ok";
const char *STATUS_TIC_TXT_NODATA = "no data";

const char *STATUS_WIFI_TXT_CONNECTING = "connecting...";
const char *STATUS_WIFI_TXT_CONNECTED  = "connected";


static TimerHandle_t s_wdt_uart;
static TimerHandle_t s_wdt_ticframe;

static EventGroupHandle_t s_to_ticled;
static QueueHandle_t s_to_oled;


static void tic_timeout()
{
    oled_update( s_to_oled, DISPLAY_TIC_STATUS, STATUS_TIC_TXT_NODATA );
}


static void uart_timeout()
{
    oled_update( s_to_oled, DISPLAY_UART_STATUS, STATUS_UART_TXT_NOSIGNAL );
    tic_timeout();   // TIC cant be up when UART receives no data
}


void status_rcv_uart( tic_mode_t mode, TickType_t next_before )
{
    // update OLED display
    const char *txt;
    switch( mode )
    {
        case TIC_MODE_HISTORIQUE:
            txt = STATUS_UART_TXT_HISTORIQUE;
            break;
        case TIC_MODE_STANDARD:
            txt = STATUS_UART_TXT_STANDARD;
            break;
        default:
            txt = STATUS_UART_TXT_NOSIGNAL;
    }

    oled_update( s_to_oled, DISPLAY_UART_STATUS, txt );
    ticled_blink_short( s_to_ticled );

    // reset uart watchdog
    if( next_before > 0 )
    {
        xTimerChangePeriod( s_wdt_uart, next_before, 1 );
    }
    xTimerReset( s_wdt_uart, 0);
}

void status_rcv_tic_frame( TickType_t next_before)
{
    oled_update( s_to_oled, DISPLAY_TIC_STATUS, STATUS_TIC_TXT_OK );
    ticled_blink_long( s_to_ticled );

    // reset tic watchdog
    if( next_before > 0 )
    {
        xTimerChangePeriod( s_wdt_ticframe, next_before, 1 );
    }
    xTimerReset( s_wdt_ticframe, 0 );

}

void status_wifi_sta_connecting( )
{
    oled_update( s_to_oled, DISPLAY_WIFI_STATUS, STATUS_WIFI_TXT_CONNECTING );

    // disbale upper layers 
    status_wifi_lost_ip();   // clear oled_ip et oled_mqtt
}


void status_wifi_sta_connected( const char *ssid )
{
    // send "SSID" to oled_ssid
    const char *txt = ( ssid == NULL ) ? STATUS_WIFI_TXT_CONNECTED : ssid;
    oled_update( s_to_oled, DISPLAY_WIFI_STATUS, txt );
    status_wifi_lost_ip();   // clear oled_ip et oled_mqtt
}


void status_wifi_got_ip( esp_netif_ip_info_t *ip_info )
{
    // send "IP = " to oled(ip)
    char buf[32];
    snprintf( buf, sizeof(buf), IPSTR, IP2STR( &(ip_info->ip) ) );
    oled_update( s_to_oled, DISPLAY_IP_ADDR, buf );
    status_mqtt_disconnected();
}


void status_wifi_lost_ip()
{
    // clear oled(ip)
    oled_update( s_to_oled, DISPLAY_IP_ADDR, "" );
    status_mqtt_disconnected();     // clear oled(mqtt)
}

void status_mqtt_connected( const char *broker )
{
    oled_update( s_to_oled, DISPLAY_IP_ADDR, "ok" );
}


void status_mqtt_disconnected()
{
    oled_update( s_to_oled, DISPLAY_MQTT_STATUS, "" );
}



void status_init( QueueHandle_t to_oled, EventGroupHandle_t to_blink)
{
    // timers d'expiration du signal serie et du decodeur TIC
    s_wdt_uart = xTimerCreate( "uart_timer", 
                                STATUS_UART_DEFAULT_TIMEOUT / portTICK_PERIOD_MS, 
                                pdFALSE, 
                                NULL, 
                                uart_timeout );

    s_wdt_ticframe = xTimerCreate( "ticframe_timer", 
                                    STATUS_TICFRAME_DEFAUT_TIMEOUT / portTICK_PERIOD_MS,
                                    pdFALSE,
                                    NULL,
                                    uart_timeout );

    // etat initial = pas de donnees serie, pas de connexion wifi

}

void status_task( void *pvParams )
{





    // 





}