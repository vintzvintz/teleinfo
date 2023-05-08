#pragma once

#include "esp_event.h"         // pour ESP_EVENT_DECLARE_BASE()


// ***************** Modes TIC ******************
typedef enum {
    TIC_MODE_INCONNU = 0,
    TIC_MODE_HISTORIQUE,
    TIC_MODE_STANDARD,
} tic_mode_t;


// ***************** Status ******************
ESP_EVENT_DECLARE_BASE(STATUS_EVENTS);         // declaration of the task events family
enum {
    STATUS_EVENT_NONE = 0,
    STATUS_EVENT_BAUDRATE,
    STATUS_EVENT_TIC_DATA,
    STATUS_EVENT_CLOCK_TICK,
    STATUS_EVENT_WIFI,
    STATUS_EVENT_MQTT,
};


//**************** datasets ****************

typedef char tic_char_t;
typedef uint32_t tic_dataset_flags_t;


// taille des buffers
#define TIC_SIZE_ETIQUETTE    16     // etiquette
#define TIC_SIZE_VALUE        128    // donnée ou horodate
#define TIC_SIZE_CHECKSUM     4      // checksum

typedef struct flagdef_s {
    tic_char_t label[TIC_SIZE_ETIQUETTE];
    tic_dataset_flags_t flags;
} flags_definition_t;

// TODO remplacer par une enum
#define TIC_DS_PUBLISHED       (1 << 0)
#define TIC_DS_HAS_TIMESTAMP   (1 << 1)
#define TIC_DS_NUMERIQUE       (1 << 2)


typedef char id_compteur_t[16];      // 12 caractères d'après la spec enedis

typedef struct {
    tic_mode_t mode;                 // mode TIC déduit des labels présents
    id_compteur_t id_compteur;
    int32_t index_energie;          // 9 car. valeur max 999 999 999 Wh -> int32 ok
    int32_t puissance_app;          // 5 car. valeur max 99 999 VA
    time_t horodate;
 } tic_data_t;


//tic_dataset_t est une liste de données décodées = contenu d'une trame complete
typedef struct dataset_s {
    tic_char_t etiquette[TIC_SIZE_ETIQUETTE];
    tic_char_t horodate[TIC_SIZE_VALUE];
    tic_char_t valeur[TIC_SIZE_VALUE];
    tic_dataset_flags_t flags;
    struct dataset_s *next;                    // linked_list
} dataset_t;


//*******************  errors **************
typedef enum tic_error_enum {
    TIC_OK = 0,
    TIC_ERR,
    TIC_ERR_APP_INIT,
    TIC_ERR_INVALID_CHAR,
    TIC_ERR_CHECKSUM,
    TIC_ERR_OVERFLOW,
    TIC_ERR_MEMORY,
    TIC_ERR_QUEUEFULL,
    TIC_ERR_UNKNOWN_DATA,
    TIC_ERR_MISSING_DATA,
    TIC_ERR_BAD_DATA,
    TIC_ERR_CONSOLE_BAD_CMD,
    TIC_ERR_NVS
} tic_error_t;



//****************** MQTT *******************

#define MQTT_TOPIC_BUFFER_SIZE 128
#define MQTT_PAYLOAD_BUFFER_SIZE 1500

typedef struct mqtt_msg_s {
    char *payload;
    char *topic;
} mqtt_msg_t;


// ***************** Oled ******************
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

