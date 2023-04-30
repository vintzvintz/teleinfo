#pragma once

#include "esp_netif.h"
#include "errors.h"

#define STATUS_BAUDRATE_DEFAULT_TIMEOUT   500     // ms
#define STATUS_TICMODE_DEFAUT_TIMEOUT     2000    // ms

typedef enum {
    TIC_MODE_INCONNU = 0,
    TIC_MODE_HISTORIQUE,
    TIC_MODE_STANDARD,
} tic_mode_t;


ESP_EVENT_DECLARE_BASE(STATUS_EVENTS);         // declaration of the task events family

enum {
    STATUS_EVENT_NONE = 0,
    STATUS_EVENT_BAUDRATE,
    STATUS_EVENT_TIC_MODE,
    STATUS_EVENT_CLOCK_TICK,
    STATUS_EVENT_PUISSANCE,
    STATUS_EVENT_WIFI,
    STATUS_EVENT_MQTT,
};


// initialise les timers d'état des liaisons
tic_error_t status_init();

// enregistre un handler pour les STATUS_EVENTS
tic_error_t status_register_event_handler (esp_event_handler_t handler_func, int32_t evt_id );

// raccourcis pour poster des évènements
tic_error_t status_update_baudrate (int baudrate, TickType_t next_before);
tic_error_t status_update_tic_mode( tic_mode_t mode, TickType_t next_before);
tic_error_t status_update_wifi (const char* ssid);
tic_error_t status_update_mqtt (const char *mqtt_status);
tic_error_t status_update_clock (const char* time_str);
tic_error_t status_update_puissance (uint32_t papp);

