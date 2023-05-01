


#define CLOCK_SERVER_NAME "fr.pool.ntp.org"




// ************** MQTT *****************************
#define MQTT_TOPIC_FORMAT "home/elec/%s"


// ******************* Pinout ***********************
// Module teleinfo 
#define UART_TELEINFO_SIGNAL_PIN   GPIO_NUM_2
#define TIC_GPIO_LED               GPIO_NUM_3

// afficheur oled 
#define OLED_GPIO_RST    -1
#define OLED_GPIO_SCL    GPIO_NUM_0
#define OLED_GPIO_SDA    GPIO_NUM_1
#define OLED_I2C_ADDRESS 0x3C


// ******************* Process ***********************
#define TIC_PROCESS_TIMEOUT   30     // secondes


// paramètres de l'application stockées en NVS (memoire flash)
#define TIC_NVS_WIFI_SSID     "wifi_ssid"
#define TIC_NVS_WIFI_PASSWORD "wifi_pwd"
#define TIC_NVS_MQTT_BROKER   "mqtt_uri"
#define TIC_NVS_MQTT_PSK_ID   "mqtt_psk_id"
#define TIC_NVS_MQTT_PSK_KEY  "mqtt_psk_key"



//**************** Status *******************
// delai max entre deux réceptions de données UART
#define STATUS_BAUDRATE_DEFAULT_TIMEOUT   500     // ms

// délai max entre deux trames correctes
#define STATUS_TICMODE_DEFAUT_TIMEOUT     2000    // ms



// **************** UART *****************
// nombre de bytes à recevoir avant de lancer le traitement
#define TIC_UART_THRESOLD  64


// ****************** WIFI **************
#define WIFI_RECONNECT_LOOP_DELAY   10000      // ms