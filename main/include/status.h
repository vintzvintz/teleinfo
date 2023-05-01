#pragma once

#include "tic_types.h"

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

