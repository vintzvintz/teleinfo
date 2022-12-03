/*
 * Contrôle de la led du PtiInfo avec un EventGroup
 */
#define TIC_BIT_COURT    ( 1 << 0 )
#define TIC_BIT_LONG     ( 1 << 1 )

#define TIC_MODE_HISTORIQUE 1
//#define TIC_MODE_STANDARD  2

typedef uint32_t tic_error_t;
typedef char tic_char_t;

/** 
 * tic_dataset_t est une liste de données décodées = contenu d'une trame complete
 */

// taille des buffers
#define TIC_SIZE_ETIQUETTE    16     // etiquette
#define TIC_SIZE_VALUE        64     // donnée ou horodate
#define TIC_SIZE_CHECKSUM     4      // checksum

typedef struct tic_dataset_s {
    tic_char_t etiquette[TIC_SIZE_ETIQUETTE];
    tic_char_t horodate[TIC_SIZE_VALUE];
    tic_char_t valeur[TIC_SIZE_VALUE];
    struct tic_dataset_s *next;                    // linked_list
} tic_dataset_t;

 /*
 * Utilisation des datasets par d'autres tâches
 */
tic_error_t tic_dataset_print( tic_dataset_t *dataset );
uint32_t tic_dataset_count( tic_dataset_t *dataset );
uint32_t tic_dataset_size( tic_dataset_t *dataset );
void tic_dataset_free( tic_dataset_t *dataset );





