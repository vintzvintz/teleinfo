/*
 *    Affection des GPIO
 */
#define TIC_GPIO_LED  GPIO_NUM_4

/*
 * Durée des clingotements
 */
#define TIC_BLINK_COURT   25         // ms
#define TIC_BLINK_LONG    2000        // ms

void blink_led_start_task( EventGroupHandle_t blink_events );
