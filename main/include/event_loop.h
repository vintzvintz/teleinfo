#pragma once

#include "tic_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// initialise les timers d'état des liaisons
tic_error_t event_loop_init();

// enregistre un handler pour les STATUS_EVENTS
//tic_error_t status_register_event_handler (esp_event_handler_t handler_func, int32_t evt_id );
tic_error_t tic_register_event_handler (int32_t event_id, esp_event_handler_t handler_func, void* handler_arg );

// raccourcis pour poster des évènements
tic_error_t send_event_baudrate (int baudrate, TickType_t next_before);
tic_error_t send_event_tic_mode( tic_mode_t mode, TickType_t next_before);
tic_error_t send_event_wifi (const char* ssid);
tic_error_t send_event_mqtt (const char *mqtt_status);
tic_error_t send_event_clock (const char* time_str);
tic_error_t send_event_puissance (uint32_t papp);

#ifdef __cplusplus
}       // extern "C" 
#endif
