



#define MQTT_OK      0
#define MQTT_ERR_OVERFLOW 1



#define MQTT_JSON_BUFFER_SIZE 1500
#define MQTT_TIC_TIMEOUT_SEC 10



typedef int mqtt_error_t;

BaseType_t mqtt_task_start( QueueHandle_t from_decoder );
