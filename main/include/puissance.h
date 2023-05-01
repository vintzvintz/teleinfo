
#include "tic_types.h"

void puissance_init();

tic_error_t puissance_incoming_data( const tic_data_t *data );

int32_t puissance_get( uint8_t n );

dataset_t * puissance_get_all();

//void puissance_debug();