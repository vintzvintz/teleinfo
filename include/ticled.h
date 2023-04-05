/*
 * Durée des clingotements
 */
#define TIC_BLINK_COURT   25         // ms
#define TIC_BLINK_LONG    2000        // ms

BaseType_t ticled_task_start();
void ticled_blink_short( );
void ticled_blink_long( );