//#include "errors.h"
#include "tic_types.h"

#ifdef __cplusplus
extern "C" {
#endif


tic_error_t oled_task_start( );
tic_error_t oled_update( display_event_type_t type, const char* txt );

#ifdef __cplusplus
}       // extern "C" 
#endif
