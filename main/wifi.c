
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

#include "errors.h"
#include "status.h"
#include "wifi.h"
#include "nvs_utils.h"
#include "tic_console.h"

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
//#define BIT_GOT_IP_BIT     BIT0
//#define BIT_AP_CONNECTED   BIT1
#define BIT_TRY_RECONNECT  BIT2
#define BIT_SCAN_DONE      BIT3 

static const char *TAG = "wifi.c";

/*
typedef struct wifi_loop_params_s {
    EventGroupHandle_t evt_group;
    esp_event_handler_instance_t handler_instance_wifi;
    esp_event_handler_instance_t handler_instance_ip;
} wifi_loop_params_t;

static wifi_loop_params_t s_wifi_params = {0};
*/

static EventGroupHandle_t s_wifi_events = NULL;


static tic_error_t set_wifi_config()
{
    wifi_config_t conf = {0};

    // get wifi credentials from NVS
    char *ssid = NULL;
    char *password = NULL;

    tic_error_t err;
    err = console_nvs_get_string( TIC_NVS_WIFI_SSID, &ssid );
    if ( (err != TIC_OK) || (ssid==NULL) || (ssid[0]=='\0') )
    {
        ESP_LOGE( TAG, "ssid wifi vide ou absent" );
        return err;
    }
    //ESP_LOGD( TAG, "ssid found in nvs (key %s) %s", TIC_NVS_WIFI_SSID, ssid);
    strncpy( (char*)conf.sta.ssid, ssid, 32);   // size hardcoded in esp_wifi_types.h
    free(ssid);

    err = console_nvs_get_string( TIC_NVS_WIFI_PASSWORD, &password );
    if ( (err != TIC_OK) || (password==NULL) || (password[0]=='\0') )
    {
        ESP_LOGE( TAG, "password wifi vide ou absent" );
        return err;
    }
    //ESP_LOGD( TAG, "password found in nvs (key %s) %s", TIC_NVS_WIFI_PASSWORD, password);
    strncpy( (char*)conf.sta.password, password, 64);   // size hardcoded in esp_wifi_types.h
    free(password);

    // Setting a password implies station will connect to all security modes including WEP/WPA.
    // However these modes are deprecated and not advisable to be used. Incase your Access point
    // doesn't support WPA2, these mode can be enabled by commenting below line 
	conf.sta.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD;
	     //.sae_pwe_h2e = WPA3_SAE_PWE_BOTH,

    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &conf) );
    ESP_LOGI( TAG, "set_wifi_config() ssid '%s' password '%s'", conf.sta.ssid, conf.sta.password );
    return TIC_OK;
}

void wifi_reconnect()
{
    ESP_LOGI (TAG, "wifi_reconnect()");
    esp_wifi_disconnect ();
    set_wifi_config ();
    status_update_wifi ("");
    xEventGroupSetBits (s_wifi_events, BIT_TRY_RECONNECT );
}

static void print_scan_results()
{
    uint16_t ap_num = 0;
    ESP_ERROR_CHECK( esp_wifi_scan_get_ap_num( &ap_num ) );
    printf( "Nb d'AP trouvés : %"PRIu16"\n", ap_num );
    if(ap_num==0) 
    { 
        return;
    }

    // recupere puis affiche les AP disponibles
    wifi_ap_record_t *ap_records = calloc(ap_num, sizeof(wifi_ap_record_t));
    if (ap_records==NULL)
    {
        ESP_LOGE( TAG, "calloc() failed");
        return;
    }
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, ap_records));

    uint16_t i;
    for (i=0; i<ap_num; i++)
    {
        const wifi_ap_record_t *ap = &ap_records[i];
        // formatte adresse MAC
        char bssid[sizeof("00:00:00:00:00:00")+1];
        snprintf( bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x", 
            ap->bssid[0],ap->bssid[1],ap->bssid[2],ap->bssid[3],ap->bssid[4],ap->bssid[5] );

        printf ("#%3"PRIi16" %32s %"PRIi8"dB  %2"PRIi8" %s\n", i+1,
                    (char*)(ap->ssid), ap->rssi, ap->primary, bssid);
    }
    free (ap_records);
}


static void handle_scan_done( const wifi_event_sta_scan_done_t *event_data )
{
    ESP_LOGD( TAG, "received WIFI_EVENT_SCAN_DONE status %"PRIu32" number %"PRIu8" scan_id %"PRIu8, 
                    event_data->status, event_data->number, event_data->scan_id );
    xEventGroupSetBits(s_wifi_events, BIT_SCAN_DONE);
}


static void handle_sta_disconnected( const wifi_event_sta_disconnected_t *event_data )
{
    // log event with information and reconnect
    char ssid[33] = {0};
    strncpy( ssid, (const char*)event_data->ssid, event_data->ssid_len+1);
    ESP_LOGD( TAG, "Wifi disconnected from %s reason %"PRIu8,
                    ssid, event_data->reason );
    wifi_reconnect();
}

static void handle_sta_connected( const wifi_event_sta_connected_t *event_data )
{
    // log event with information
    char ssid[33] = {0};
    assert (event_data->ssid_len <= 32);
    strncpy( ssid, (char*)event_data->ssid, event_data->ssid_len + 1);
    ESP_LOGI(TAG, "Wifi connected to ssid '%s', channel %02"PRIu8" authmode %02x", 
                ssid, event_data->channel, event_data->authmode );

    // update system status and events
    status_update_wifi( ssid );
    xEventGroupClearBits( s_wifi_events, BIT_TRY_RECONNECT );

}


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    switch( event_id )
    {
    case WIFI_EVENT_WIFI_READY:               // ESP32 WiFi ready 
        ESP_LOGD( TAG, "WIFI_EVENT_WIFI_READY" );
        break;

    case WIFI_EVENT_SCAN_DONE:                // ESP32 finish scanning AP 
        handle_scan_done( (wifi_event_sta_scan_done_t *) event_data);
        break;
    
    case WIFI_EVENT_STA_START:                
        ESP_LOGD( TAG, "WIFI_EVENT_STA_START" );
        wifi_reconnect( );
        break;

    case WIFI_EVENT_STA_STOP:                 
        ESP_LOGD( TAG, "WIFI_EVENT_STA_STOP" );
        break;

    case WIFI_EVENT_STA_CONNECTED:
        handle_sta_connected( (wifi_event_sta_connected_t *)event_data );
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
        handle_sta_disconnected( (wifi_event_sta_disconnected_t *)event_data);
        break;

    case WIFI_EVENT_STA_AUTHMODE_CHANGE:      // the auth mode of AP connected by ESP32 station changed
        ESP_LOGD( TAG, "WIFI_EVENT_STA_AUTHMODE_CHANGE" );
        break;

    default:
        ESP_LOGD( TAG, "WIFI_EVENT id=%#lx", event_id );
    }
}

/*
static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;

    switch( event_id ) 
    {
    case IP_EVENT_STA_GOT_IP:               // !< station got IP from connected AP 
        ESP_LOGI(TAG, "Got IP " IPSTR, IP2STR(&event->ip_info.ip));
//        status_wifi_got_ip( &(event->ip_info) );
        break;

    case IP_EVENT_STA_LOST_IP:              // !< station lost IP and the IP is reset to 0
        ESP_LOGI(TAG, "IP_EVENT_LOST_IP");
//        status_wifi_lost_ip();
        wifi_reconnect();
        break;

    default:
        ESP_LOGD( TAG, "IP_EVENT id=%#lx", event_id );
    }
}
*/



static tic_error_t wifi_initialise()
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
                                                          NULL ) );
/*
    ESP_ERROR_CHECK( esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         &ip_event_handler,
                                                         NULL,
                                                         &(s_wifi_params.handler_instance_ip)));

*/
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );   // ne pas stocker les params wifi dans la flash
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_LOGI(TAG, "wifi_init_sta finished.");
    return TIC_OK;
}


tic_error_t wifi_scan_start(int timeout_sec)
{
    // start scan
    ESP_LOGD( TAG, "Scan wifi lancé (timeout %i sec)", timeout_sec);

    ESP_ERROR_CHECK (esp_wifi_set_mode(WIFI_MODE_STA) );
    wifi_scan_config_t scan_conf = { 0 };   // default params
    ESP_ERROR_CHECK (esp_wifi_scan_start( &scan_conf, 0 ) );
    xEventGroupClearBits (s_wifi_events, BIT_SCAN_DONE);
    return TIC_OK;
}


static void wifi_loop( void * pvParams )
{
    ESP_LOGD( TAG, "wifi_loop()" );
    for(;;)
    {
        EventBits_t bits = xEventGroupWaitBits( s_wifi_events, BIT_TRY_RECONNECT|BIT_SCAN_DONE, pdFALSE, pdFALSE, portMAX_DELAY );

        if (bits & BIT_TRY_RECONNECT)
        {
            // ne pas clear BIT_TRY_RECONNECT pour retenter une connexion après WIFI_RECONNECT_LOOP_DELAY
            // si aucun STA_CONNECTED_EVENT n'est reçu.
            ESP_LOGD( TAG, "esp_wifi_connect()" );
            esp_err_t err = esp_wifi_connect();
            if( err != ESP_OK )
            {
                ESP_LOGE( TAG, "esp_wifi_connect() error %#x. Retry" , err );
                wifi_reconnect();
            }
            vTaskDelay( WIFI_RECONNECT_LOOP_DELAY / portTICK_PERIOD_MS );   // delai mini entre 2 reconnexions
        }

        if (bits & BIT_SCAN_DONE)
        {
            xEventGroupClearBits( s_wifi_events, BIT_SCAN_DONE );
            print_scan_results();
        }
    }
    ESP_LOGE( TAG, "Erreur : wifi_loop exited");
}


void wifi_task_start( )
{
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    // FreeRTOS event group to signal when we are connected
    s_wifi_events = xEventGroupCreate();
    if( s_wifi_events == NULL )
    {
        ESP_LOGE( TAG, "Could not create wifi event group" );
        return;
    }

    if( wifi_initialise() != TIC_OK )
    {
        ESP_LOGE( TAG, "erreur wifi_initialise()" );
        return;
    }

    xTaskCreate( wifi_loop, "wifi_task", 4096, NULL, 10, NULL );
}