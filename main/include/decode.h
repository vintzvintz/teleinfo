
#pragma once

#include "tic_types.h"

/* reception des bytes depuis uart_task */
tic_error_t decode_incoming_bytes (tic_char_t *buf , size_t len, tic_mode_t mode);

/* creation initiale de la tache */
void tic_decode_task_start( );
