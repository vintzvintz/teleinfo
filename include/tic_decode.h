


#define TIC_MODE_HISTORIQUE 1
//#define TIC_MODE_STANDARD  1

typedef uint32_t tic_error_t ;

typedef char tic_char_t;

// taille des buffers
#define TIC_SIZE_ETIQUETTE    16     // etiquette
#define TIC_SIZE_VALUE        64     // donnée ou horodate
#define TIC_SIZE_CHECKSUM     4      // checksum

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


/** 
 * tic_dataset_t est une liste de données décodées = contenu d'une trame complete
 */
typedef struct tic_dataset_s {
    tic_char_t etiquette[TIC_SIZE_ETIQUETTE];
    tic_char_t horodate[TIC_SIZE_VALUE];
    tic_char_t valeur[TIC_SIZE_VALUE];
    struct tic_dataset_s *next;                    // linked_list
} tic_dataset_t;

tic_error_t tic_dataset_print( tic_dataset_t *dataset );

uint32_t tic_dataset_count( tic_dataset_t *dataset );
uint32_t tic_dataset_size( tic_dataset_t *dataset );

void tic_dataset_free( tic_dataset_t *dataset );

void tic_decode_start_task( StreamBufferHandle_t from_uart, QueueHandle_t mqtt_queue );
