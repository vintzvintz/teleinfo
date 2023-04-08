
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/event_groups.h"

#include "esp_netif.h"
#include "esp_log.h"

/* Intellisense bullshit */
#undef __linux__

// lwIP component
#include "esp_sntp.h"

#include "clock.h"
#include "status.h"

static const char *TAG = "timesync";

EventGroupHandle_t s_clock_evt;

#define SYNC_CLOCK_BIT       BIT0
#define MAX_RESYNC_INTERVAL  5
//#define TZSTRING_CET         "CET-1CEST,M3.5.0/2,M10.5.0/3"    // [Europe/Paris]


void clock_lost()
{
    xEventGroupClearBits( s_clock_evt, SYNC_CLOCK_BIT );
}


void sntp_callback( struct timeval *tv )
{
    // clock will be marked lost if not resynced periodically
    uint32_t clock_lost_delay = sntp_get_sync_interval() * MAX_RESYNC_INTERVAL ;
    xTimerCreate( "clock_watchdog", clock_lost_delay / portTICK_PERIOD_MS, pdFALSE, NULL, clock_lost );
    ESP_LOGI( TAG, "Got NTP time from %s. Sync will be lost in %lu seconds", CLOCK_SERVER_NAME, clock_lost_delay / 1000 );

    // log full time+date
    time_t now = tv->tv_sec;
    struct tm timeinfo;
    char buf[60];
    localtime_r( &now, &timeinfo );
    strftime( buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo );
    ESP_LOGI( TAG, "local time %s", buf );

    // notify clock loop
    xEventGroupSetBits( s_clock_evt, SYNC_CLOCK_BIT );
}

void clock_task( void *pvParams )
{
    ESP_LOGI( TAG, "clock_task()" );

    time_t now;
    struct tm timeinfo;
    char buf[20];
    for(;;)
    {
        EventBits_t bits = xEventGroupWaitBits( s_clock_evt, SYNC_CLOCK_BIT, pdFALSE, pdFALSE, 1000 / portTICK_PERIOD_MS );
        if( bits & SYNC_CLOCK_BIT )
        {
            // synchronised
            now = time(NULL);
            localtime_r( &now, &timeinfo );
            strftime( buf, sizeof(buf), "%H:%M:%S", &timeinfo );
            status_clock_update( buf );
        }
        else
        {
            // not synchronised
            status_clock_update( "--:--:--" );
        }
        vTaskDelay( 1000 / portTICK_PERIOD_MS );
    }
}


void clock_task_start( )
{
    ESP_LOGI( TAG, "clock_task_start()");

    s_clock_evt = xEventGroupCreate();

    // initialize SNTP client
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, CLOCK_SERVER_NAME );
    //setenv( "TZ", TZSTRING_CET, 1);
    //tzset();
    sntp_set_time_sync_notification_cb( sntp_callback );
    sntp_init();

    // create clock client task
    BaseType_t task_created = xTaskCreate( clock_task, "clock_task", 4096, NULL, 10, NULL);
    if( task_created != pdPASS )
    {
        ESP_LOGE( TAG, "xTaskCreate() failed");
    }
}
