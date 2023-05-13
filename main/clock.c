
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/event_groups.h"


#ifdef CONFIG_TIC_SNTP

#include "esp_netif.h"
#include "esp_log.h"


// lwIP component
#include "esp_sntp.h"

#include "clock.h"
#include "tic_config.h"    // pour l'adresse du serveur SNTP
#include "event_loop.h"

// from Kconfig
#define CLOCK_SERVER_NAME CONFIG_TIC_SNTP_SERVER

static const char *TAG = "clock.c";

TimerHandle_t s_clock_wdt = NULL;

#define SYNC_CLOCK_BIT       BIT0
#define MAX_MISSED_RESYNC    5


void clock_lost()
{
    ESP_LOGW( TAG, "SNTP sync is lost" );
}


void sntp_callback( struct timeval *tv )
{
    // log full time+date
    time_t now = tv->tv_sec;
    struct tm timeinfo;
    char buf[60];
    localtime_r( &now, &timeinfo );
    strftime( buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo );
    ESP_LOGI( TAG, "Got NTP time %s from %s", buf, CLOCK_SERVER_NAME );

    if (s_clock_wdt)
    {
        xTimerReset( s_clock_wdt, 10 );   // ignore errors
        TickType_t t = xTimerGetPeriod (s_clock_wdt);
        ESP_LOGD( TAG, "Reset sntp watchdog to %lu seconds", (1000* t / portTICK_PERIOD_MS) );
    }
}



// TODO -> déplacer dans oled.cpp  ?
void clock_task( void *pvParams )
{
    time_t now;
    struct tm timeinfo;
    char buf[20];
    for(;;)
    {
        if ( xTimerIsTimerActive (s_clock_wdt) )
        {
            // synchronised
            now = time(NULL);
            localtime_r( &now, &timeinfo );
            strftime( buf, sizeof(buf), "%H:%M:%S", &timeinfo );
            send_event_clock( buf );
        }
        else
        {
            // not synchronised
            send_event_clock( "no sntp" );
        }
        vTaskDelay( 1000 / portTICK_PERIOD_MS );
    }
}


tic_error_t clock_task_start()
{
    ESP_LOGD( TAG, "clock_task_start()");

    // initialize SNTP client
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, CLOCK_SERVER_NAME );
    sntp_set_time_sync_notification_cb( sntp_callback );
    sntp_init();

    // timer will expire after too much missed resync
    uint32_t clock_lost_delay = sntp_get_sync_interval() * MAX_MISSED_RESYNC ;
    s_clock_wdt = xTimerCreate( "clock_watchdog", clock_lost_delay / portTICK_PERIOD_MS, pdFALSE, NULL, clock_lost );
    if( !s_clock_wdt )
    {
        ESP_LOGE( TAG, "xTimerCreate() failed");
        return TIC_ERR_APP_INIT;
    }

    // create clock client task
    BaseType_t task_created = xTaskCreate( clock_task, "clock_task", 4096, NULL, 10, NULL);
    if( task_created != pdPASS )
    {
        ESP_LOGE( TAG, "xTaskCreate() failed");
        return TIC_ERR_APP_INIT;
    }
    return TIC_OK;
}


#endif // CONFIG_TIC_SNTP
