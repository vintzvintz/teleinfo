

// alias pour les tailles de buffers
#define TIC_SIZE_BUF0 TIC_SIZE_ETIQUETTE
#define TIC_SIZE_BUF1 TIC_SIZE_VALUE
#define TIC_SIZE_BUF2 TIC_SIZE_VALUE
#define TIC_SIZE_BUF3 TIC_SIZE_CHECKSUM

// nb maxi de datasets dans une trame
#define TIC_MAX_DATASETS    99

// erreurs
#define TIC_OK 0
#define TIC_ERR 1
#define TIC_ERR_INVALID_CHAR 2
#define TIC_ERR_CHECKSUM 3
#define TIC_ERR_OVERFLOW  4
#define TIC_ERR_MEMORY 5
#define TIC_ERR_QUEUEFULL 6


#define RX_BUF_SIZE 128


void tic_decode_start_task( StreamBufferHandle_t from_uart, QueueHandle_t mqtt_queue, EventGroupHandle_t blink_events, QueueHandle_t to_oled );
