/*
 *    Affection des GPIO
 */
#define TIC_GPIO_LED  GPIO_NUM_4

/*
 * Durée des clingotements
 */
#define TIC_BLINK_COURT   25         // ms
#define TIC_BLINK_LONG    2000        // ms

void ticled_start_task( EventGroupHandle_t to_ticled );


void ticled_blink_short( EventGroupHandle_t to_ticled );
void ticled_blink_long( EventGroupHandle_t to_ticled );