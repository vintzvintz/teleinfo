
#pragma once

#include "errors.h"    // pour tic_error_t
#include "status.h"    // pour tic_mode_t
#include "dataset.h"   // pour tic_char_t

/* reception des bytes depuis uart_task */
tic_error_t decode_incoming_bytes (tic_char_t *buf , size_t len, tic_mode_t mode);

/* creation initiale de la tache */
void tic_decode_task_start( );
