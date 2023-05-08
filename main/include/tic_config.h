


// ************** MQTT *****************************
#define MQTT_TOPIC_FORMAT "home/elec/%s"

// ******************* Process ***********************
#define TIC_PROCESS_TIMEOUT   30     // secondes


// paramètres de l'application stockées en NVS (memoire flash)
#define TIC_NVS_WIFI_SSID     "wifi_ssid"
#define TIC_NVS_WIFI_PASSWORD "wifi_pwd"
#define TIC_NVS_MQTT_BROKER   "mqtt_uri"
#define TIC_NVS_MQTT_PSK_ID   "mqtt_psk_id"
#define TIC_NVS_MQTT_PSK_KEY  "mqtt_psk_key"

//**************** Status *******************
// délai max entre deux trames correctes
#define TIC_DECODE_TIMEOUT     3000    // ms

// **************** UART *****************
// nombre de bytes à recevoir avant de lancer le traitement
#define TIC_UART_THRESOLD  64

// ****************** WIFI **************
#define WIFI_RECONNECT_LOOP_DELAY   10000      // ms