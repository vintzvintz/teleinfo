#ifdef __cplusplus
extern "C" {
#endif


#define OLED_GPIO_RST    -1
#define OLED_GPIO_SCL    GPIO_NUM_5
#define OLED_GPIO_SDA    GPIO_NUM_6
#define OLED_I2C_ADDRESS 0x3C


typedef enum display_event_type_e {
    DISPLAY_NO_UPDATE = 0,
    DISPLAY_UART_STATUS,
    DISPLAY_TIC_STATUS,
    DISPLAY_WIFI_STATUS,
    DISPLAY_IP_ADDR,
    DISPLAY_MQTT_STATUS,
    DISPLAY_PAPP,
    DISPLAY_MESSAGE
} display_event_type_t;

#define DISPLAY_EVENT_DATA_SIZE 32

typedef struct display_event_s {
    display_event_type_t info;
    char txt[DISPLAY_EVENT_DATA_SIZE];
} display_event_t;


void oled_task_start( QueueHandle_t to_oled );
void oled_demo_task_start(void);

void oled_update( QueueHandle_t to_oled, display_event_type_t type, const char* txt );

#ifdef __cplusplus
}       // extern "C" 
#endif
