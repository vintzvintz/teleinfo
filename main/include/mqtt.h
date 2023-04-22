
#include "freertos/FreeRTOS.h"
#include "errors.h"

#define MQTT_TOPIC_FORMAT "home/elec/%s"

#define MQTT_TOPIC_BUFFER_SIZE 128
#define MQTT_PAYLOAD_BUFFER_SIZE 1500


typedef struct mqtt_msg_s {
    char *payload;
    char *topic;
} mqtt_msg_t;


// Alloue un message mqtt
mqtt_msg_t * mqtt_msg_alloc();

// libere un message mqtt
void mqtt_msg_free(mqtt_msg_t *msg);

// place un message MQTT dans la queue d'envoi du client mqtt
tic_error_t mqtt_receive_msg( mqtt_msg_t *msg);

// appelle esp_mqtt_client_reconnect()
tic_error_t mqtt_client_restart();

// Initialise le client MQTT et lance la tache associee
BaseType_t mqtt_task_start(int dummy);
//BaseType_t mqtt_dummy_task_start( );

