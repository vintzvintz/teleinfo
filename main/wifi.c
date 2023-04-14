
/* Intellisense bullshit */
#undef __linux__

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "status.h"
#include "wifi.h"

#include "wifi_credentials.h"
/*
#define TIC_WIFI_SSID      "your_ssid"
#define TIC_WIFI_PASSWORD  "your_wifi_password"
*/


#if CONFIG_TIC_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_TIC_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_TIC_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_TIC_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_TIC_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_TIC_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_TIC_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_TIC_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif


// Bits utiles pour le EventGroup
#define GOT_IP_BIT              BIT0
#define WIFI_CONNECTED_BIT      BIT1
#define WIFI_TRY_RECONNECT_BIT  BIT2      // déclenche une reconnection wifi

static const char *TAG = "wifi station";

typedef struct wifi_loop_params_s {
    EventGroupHandle_t evt_group;
    esp_event_handler_instance_t handler_instance_wifi;
    esp_event_handler_instance_t handler_instance_ip;
} wifi_loop_params_t;

static wifi_loop_params_t s_wifi_params = {0};

/*
// pour synchronisation d'autres tâches
BaseType_t wifi_wait_ipaddr()
{
    ESP_LOGI( TAG, "wifi_wait_ipaddr()");
    EventBits_t bits = xEventGroupWaitBits( s_wifi_params.evt_group, GOT_IP_BIT | WIFI_CONNECTED_BIT , pdFALSE, pdTRUE, portMAX_DELAY );
    return ( bits & ( GOT_IP_BIT | WIFI_CONNECTED_BIT ) ) ? pdTRUE : pdFALSE;
}
*/

static void reconnect()
{
    ESP_LOGD( TAG, "reconnect()" );
    status_wifi_sta_connecting();
    xEventGroupClearBits( s_wifi_params.evt_group, (GOT_IP_BIT | WIFI_CONNECTED_BIT) );
    xEventGroupSetBits( s_wifi_params.evt_group, WIFI_TRY_RECONNECT_BIT );
}


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    switch( event_id )
    {
    case WIFI_EVENT_WIFI_READY:               /**< ESP32 WiFi ready */
        ESP_LOGD( TAG, "WIFI_EVENT_WIFI_READY" );
        break;

    case WIFI_EVENT_SCAN_DONE:                /**< ESP32 finish scanning AP */
        ESP_LOGD( TAG, "WIFI_EVENT_SCAN_DONE" );
        break;
    
    case WIFI_EVENT_STA_START:                /**< ESP32 station start */
        ESP_LOGD( TAG, "WIFI_EVENT_STA_START" );
        reconnect( );
        break;

    case WIFI_EVENT_STA_STOP:                 /**< ESP32 station stop */
        ESP_LOGD( TAG, "WIFI_EVENT_STA_STOP" );
        break;

    case WIFI_EVENT_STA_CONNECTED:            /**< ESP32 station connected to AP */
        ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED to ap SSID:%s", TIC_WIFI_SSID );
        status_wifi_sta_connected( TIC_WIFI_SSID );
        xEventGroupClearBits( s_wifi_params.evt_group, WIFI_TRY_RECONNECT_BIT );
        xEventGroupSetBits( s_wifi_params.evt_group, WIFI_CONNECTED_BIT );
        break;

    case WIFI_EVENT_STA_DISCONNECTED:         /**< ESP32 station disconnected from AP */
        ESP_LOGD( TAG, "WIFI_EVENT_STA_DISCONNECTED" );
        reconnect( );
        break;

    case WIFI_EVENT_STA_AUTHMODE_CHANGE:      /**< the auth mode of AP connected by ESP32 station changed */
        ESP_LOGD( TAG, "WIFI_EVENT_STA_AUTHMODE_CHANGE" );
        break;

    default:
        ESP_LOGD( TAG, "WIFI_EVENT id=%#lx", event_id );
    }
}


static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;

    switch( event_id ) 
    {
    case IP_EVENT_STA_GOT_IP:               /*!< station got IP from connected AP */
        ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP :" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits( s_wifi_params.evt_group, GOT_IP_BIT );
        status_wifi_got_ip( &(event->ip_info) );
        break;

    case IP_EVENT_STA_LOST_IP:              /*!< station lost IP and the IP is reset to 0 */
        ESP_LOGI(TAG, "IP_EVENT_LOST_IP");
        status_wifi_lost_ip();
        reconnect();
        break;

    default:
        ESP_LOGD( TAG, "IP_EVENT id=%#lx", event_id );
    }
}


int wifi_initialise()
{
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK( esp_event_handler_instance_register( WIFI_EVENT,
                                                          ESP_EVENT_ANY_ID,
                                                          &wifi_event_handler,
                                                          NULL,
                                                          &(s_wifi_params.handler_instance_wifi)));

    ESP_ERROR_CHECK( esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         &ip_event_handler,
                                                         NULL,
                                                         &(s_wifi_params.handler_instance_ip)));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = TIC_WIFI_SSID,
            .password = TIC_WIFI_PASSWORD,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
	     //.sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_LOGI(TAG, "wifi_init_sta finished.");
    return ESP_OK;
}

void wifi_loop( void * pvParams )
{
    ESP_LOGD( TAG, "wifi_loop()" );

    for(;;)
    {
        EventBits_t bits = xEventGroupWaitBits( s_wifi_params.evt_group, WIFI_TRY_RECONNECT_BIT, pdFALSE, pdFALSE, portMAX_DELAY );
        if( bits & (GOT_IP_BIT|WIFI_CONNECTED_BIT) )
        {
            ESP_LOGE( TAG, "GOT_IP_BIT or WIFI_CONNECTED_BIT not cleared before reconnection attempt");
        }
        xEventGroupClearBits( s_wifi_params.evt_group, (GOT_IP_BIT | WIFI_CONNECTED_BIT) );

        ESP_LOGD( TAG, "esp_wifi_connect()" );
        esp_err_t err = esp_wifi_connect();
        if( err != ESP_OK )
        {
            ESP_LOGE( TAG, "esp_wifi_connect() error %#x" , err );
            reconnect();
        }
        vTaskDelay( WIFI_RECONNECT_LOOP_DELAY / portTICK_PERIOD_MS );   // wait for typical connection time before retrying
    }
    ESP_LOGE( TAG, "Erreur : wifi_loop exited");
}


void wifi_task_start( )
{
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    // FreeRTOS event group to signal when we are connected
    s_wifi_params.evt_group = xEventGroupCreate();
    if( s_wifi_params.evt_group == NULL )
    {
        ESP_LOGE( TAG, "Could not create wifi event group" );
        return;
    }

    if( wifi_initialise() < 0 )
    {
        ESP_LOGE( TAG, "erreur wifi_initialise()" );
        return;
    }

    xTaskCreate( wifi_loop, "wifi_task", 4096, NULL, 10, NULL );
}