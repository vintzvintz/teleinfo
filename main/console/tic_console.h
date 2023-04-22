
// paramètres de l'application stockées en NVS (memoire flash)
#define TIC_NVS_WIFI_SSID     "wifi_ssid"
#define TIC_NVS_WIFI_PASSWORD "wifi_pwd"


#define TIC_NVS_MQTT_BROKER   "mqtt_uri"
#define TIC_NVS_MQTT_PSK_ID   "mqtt_psk_id"
#define TIC_NVS_MQTT_PSK_KEY  "mqtt_psk_key"


//*****************
// console.c
//*****************
BaseType_t console_task_start();


//*****************
// cmd_tic.c
//*****************

void console_register_commands();


//*****************
// cmd_nvs.c
//*****************

void console_register_nvs();  



//*****************
// cmd_system.c
//*****************
void console_register_system(void);

// Register all system functions
//void register_system(void);

// Register common system functions: "version", "restart", "free", "heap", "tasks"
//void register_system_common(void);

// Register deep and light sleep functions
//void register_system_sleep(void);
