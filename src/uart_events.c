
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "pinout.h"
#include "uart_events.h"
#include "decode.h"
#include "status.h"


static const char *TAG = "uart_events";

#define BUF_SIZE 512
#define RD_BUF_SIZE (BUF_SIZE)


/*
static const uart_config_t uart_config_mode_historique = {
    .baud_rate = 1200,
    .data_bits = UART_DATA_7_BITS,
    .parity = UART_PARITY_EVEN,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_APB,
};
*/

static const uart_config_t uart_config_mode_standard = {
    .baud_rate = 9600,
    .data_bits = UART_DATA_7_BITS,
    .parity = UART_PARITY_EVEN,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_APB,
};

static QueueHandle_t uart1_queue;

/*
typedef struct uart_task_params_s {
    StreamBufferHandle_t to_decoder;

} uart_task_params_t;
*/

static void uart_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE);

    //uart_task_params_t *task_params = (uart_task_params_t *)pvParameters;
    int length_read, length_sent;

    for(;;) {
        //Waiting for UART event.
        if(xQueueReceive(uart1_queue, (void * )&event, portMAX_DELAY)) {
            memset(dtmp, 0, RD_BUF_SIZE);

            switch(event.type) {
                //Event of UART receving data
                case UART_DATA:
                    ESP_LOGD(TAG, "[UART DATA]: %d bytes", event.size);
                    length_read = uart_read_bytes(UART_TELEINFO_NUM, dtmp, event.size, portMAX_DELAY);
                    length_sent = decode_receive_bytes(dtmp, length_read );
                    if( length_sent != length_read )
                    {
                        ESP_LOGE( TAG, "%d bytes perdus sur %d reçus", (length_read-length_sent), length_read);
                    }
                    //xStreamBufferSend( task_params->to_decoder, dtmp, length_read, portMAX_DELAY);
                    status_rcv_uart( TIC_MODE_STANDARD, 0 );
                    //printf("%s",dtmp);
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGI(TAG, "hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(UART_TELEINFO_NUM);
                    xQueueReset(uart1_queue);
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGI(TAG, "ring buffer full");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(UART_TELEINFO_NUM);
                    xQueueReset(uart1_queue);
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    ESP_LOGI(TAG, "uart rx break");
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    ESP_LOGI(TAG, "uart parity error");
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    ESP_LOGI(TAG, "uart frame error");
                    break;
                //Others
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}


//static uart_task_params_t uart_task_params;


//void uart_task_start( StreamBufferHandle_t streambuf_to_decoder )
void uart_task_start( )
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */

    //Set UART log level
    esp_log_level_set(TAG, ESP_LOG_INFO);

    //Install UART driver, and get the queue.
    ESP_LOGD( TAG, "uart_driver_install()" );
    uart_driver_install(UART_TELEINFO_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart1_queue, 0);
    uart_param_config(UART_TELEINFO_NUM, &uart_config_mode_standard);
    uart_set_pin(UART_TELEINFO_NUM, UART_PIN_NO_CHANGE, UART_TELEINFO_SIGNAL_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_set_rx_full_threshold( UART_TELEINFO_NUM, TIC_UART_THRESOLD);

    //Create a task to handler UART event from ISR
    //uart_task_params.to_decoder = streambuf_to_decoder;

    //ESP_LOGD( TAG, "uart_xTaskCreate()" );
    xTaskCreate(uart_task, "uart_task", 8192, NULL, 12, NULL);
}

