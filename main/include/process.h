
#include "dataset.h"

#define TIC_PROCESS_TIMEOUT   30     // secondes


#define TIC_PROCESS_TOPIC_BUFFER_SIZE 128
#define TIC_PROCESS_JSON_BUFFER_SIZE 1500


tic_error_t process_receive_datasets( dataset_t *ds );

BaseType_t process_task_start( );