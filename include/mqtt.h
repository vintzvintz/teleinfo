

#define MQTT_TOPIC_FORMAT "home/elec/%s"


#define MQTT_TOPIC_BUFFER_SIZE 128
#define MQTT_PAYLOAD_BUFFER_SIZE 1500
#define MQTT_TIC_TIMEOUT_SEC 10

#define BROKER_HOST   "teleinfo.vintz.fr"
#define BROKER_PORT   8883
#define PSK_IDENTITY  "vintz"
#define PSK_KEY       {0x51, 0x51, 0x51, 0x51}

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

// Initialise le client MQTT et lance la tache associee
BaseType_t mqtt_task_start();
