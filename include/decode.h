
#pragma once

#include "errors.h"    // pour tic_error_t

// buffer de reception - alimenté par la tâche uart
#define DECODE_RCV_BUFFER_SIZE     512
#define DECODE_RCV_BUFFER_TRIGGER   16


// taille des buffers
#define TIC_SIZE_ETIQUETTE    16     // etiquette
#define TIC_SIZE_VALUE        128    // donnée ou horodate
#define TIC_SIZE_CHECKSUM     4      // checksum

// nb maxi de datasets dans une trame
#define TIC_MAX_DATASETS    99

#define RX_BUF_SIZE 128

typedef char tic_char_t;
typedef uint32_t tic_dataset_flags_t;


//tic_dataset_t est une liste de données décodées = contenu d'une trame complete
typedef struct tic_dataset_s {
    tic_char_t etiquette[TIC_SIZE_ETIQUETTE];
    tic_char_t horodate[TIC_SIZE_VALUE];
    tic_char_t valeur[TIC_SIZE_VALUE];
    tic_dataset_flags_t flags;
    struct tic_dataset_s *next;                    // linked_list
} tic_dataset_t;

 /*
 * Utilisation des datasets par d'autres tâches
 */
tic_error_t tic_dataset_print( tic_dataset_t *dataset );
uint32_t tic_dataset_count( tic_dataset_t *dataset );
uint32_t tic_dataset_size( tic_dataset_t *dataset );
void tic_dataset_free( tic_dataset_t *dataset );
tic_dataset_t * tic_dataset_sort(tic_dataset_t *ds);
const tic_dataset_t* tic_dataset_find( const tic_dataset_t *ds, const char *etiquette );

/* reception des bytes depuis uart_task */
size_t decode_receive_bytes( void *buf , size_t length );

/* creation initiale de la tache */
void tic_decode_task_start( );
