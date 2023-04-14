#ifdef __cplusplus
extern "C" {
#endif


typedef enum display_event_type_e {
    DISPLAY_TIC_STATUS =0,
    DISPLAY_WIFI_STATUS,
    DISPLAY_IP_ADDR,
    DISPLAY_MQTT_STATUS,
    DISPLAY_PAPP,
    DISPLAY_CLOCK,
    DISPLAY_MESSAGE,
    DISPLAY_EVENT_TYPE_MAX
} display_event_type_t;


#define DISPLAY_EVENT_DATA_SIZE 32

typedef struct display_event_s {
    display_event_type_t info;
    char txt[DISPLAY_EVENT_DATA_SIZE];
} display_event_t;


BaseType_t oled_task_start( );
BaseType_t oled_update( display_event_type_t type, const char* txt );

#ifdef __cplusplus
}       // extern "C" 
#endif
