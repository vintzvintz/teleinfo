
#pragma once

#include "errors.h"    // pour tic_error_t
#include "dataset.h"

// buffer de reception - alimenté par la tâche uart
#define DECODE_RCV_BUFFER_SIZE     512
#define DECODE_RCV_BUFFER_TRIGGER   16

/*
// taille des buffers
#define TIC_SIZE_ETIQUETTE    16     // etiquette
#define TIC_SIZE_VALUE        128    // donnée ou horodate
#define TIC_SIZE_CHECKSUM     4      // checksum
*/
// nb maxi de datasets dans une trame
#define TIC_MAX_DATASETS    99

#define RX_BUF_SIZE 128


/* reception des bytes depuis uart_task */
size_t decode_receive_bytes( void *buf , size_t length );

/* creation initiale de la tache */
void tic_decode_task_start( );
