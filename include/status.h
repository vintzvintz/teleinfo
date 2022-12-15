



#define STATUS_UART_DEFAULT_TIMEOUT       500     // ms
#define STATUS_TICFRAME_DEFAUT_TIMEOUT   2000    // ms


typedef enum {
    TIC_MODE_INCONNU = 0,
    TIC_MODE_HISTORIQUE,
    TIC_MODE_STANDARD,
} tic_mode_t;

// pour initialiser les pointeurs vers les périphériques
void status_init( QueueHandle_t to_oled, EventGroupHandle_t to_ticled );

// pour notifier la réception de données par l'UART
void status_rcv_uart( tic_mode_t mode, TickType_t next_before );

// pour notifier la réception d'une trames TIC valide
void status_rcv_tic_frame( TickType_t next_before);

// pour notifier qu'une tentative de connexion à un AP wifi est en cours, puis réussie
void status_wifi_sta_connecting();
void status_wifi_sta_connected( const char *ssid );

// pour notifier qu'une adresse IP est obtenue/perdue
void status_wifi_got_ip( esp_netif_ip_info_t *ip_info );
void status_wifi_lost_ip();

// pour notifier l'état du client MQTT
void status_mqtt_update( const char *status );

// pour afficher la puissance apparente instantanée
void status_papp_update( int papp );

// affiche l'heure courante
void status_clock_update( const char *now );