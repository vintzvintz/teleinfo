
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"


#include "tic_types.h"
#include "tic_config.h"     // uart rx pin
#include "uart_events.h"
#include "decode.h"         // pour decode_incming_bytes()
#include "status.h"         // pour status_update_baudrate()


static const char *TAG = "uart_events.c";

#define BUF_SIZE 512
#define RD_BUF_SIZE (BUF_SIZE)

#define UART_TELEINFO_NUM  UART_NUM_1

#define BAUD_RATE_MODE_HISTORIQUE      1200
#define BAUD_RATE_MODE_STANDARD        9600

// nb d'evènements FRAME_ERR ou PARITY_ERR pour changer le baudrate
#define MAX_ERRORS_CNT                 10

// délai minimum entre deux changements de baudrate
#define TOGGLE_TIMER_DELAY_MS          1000


static QueueHandle_t s_uart1_queue = NULL;
static TimerHandle_t s_toggle_timer = NULL;

static EventGroupHandle_t s_toggle_event = NULL;
#define BIT_TOGGLE_REQUEST      BIT0
#define BIT_TOGGLE_ENABLE       BIT1


static void enable_toggle( TimerHandle_t pxTimer )
{
    if(s_toggle_event)
    {
        xEventGroupSetBits (s_toggle_event, BIT_TOGGLE_ENABLE);
        ESP_LOGD (TAG, "enable toggle_baudrate()");
    }
}


/// @brief  change le baudrate
/// @return true si la demande de changement est prise en compte
///         false si la demande est ignorée
static bool toggle_baudrate()
{
    if (!s_toggle_event)
    {
        //ESP_LOGD (TAG, "toggle_baudrate() : rejeté car s_toggle_event=%p", (void*)s_toggle_event);
        return false;
    }

    EventBits_t bits = xEventGroupGetBits (s_toggle_event);
    if( !(bits & BIT_TOGGLE_ENABLE) )
    {
     //   ESP_LOGD (TAG, "toggle_baudrate() : rejeté car not enabled");
        return false;
    }

    ESP_LOGD (TAG, "toggle_baudrate() : ok");
    xEventGroupSetBits( s_toggle_event, BIT_TOGGLE_REQUEST );
    return true;
}




static int get_baudrate()
{
    uint32_t baudrate = 0;
    esp_err_t err = uart_get_baudrate(UART_TELEINFO_NUM, &baudrate);   // ignore error
    if( err != ESP_OK )
    {
        ESP_LOGE (TAG, "get_baudrate() erreur %#x", err);
        return 0;
    }
    return (int)baudrate;
}



static int get_tic_mode()
{
    int baudrate = get_baudrate();
    if (baudrate == BAUD_RATE_MODE_STANDARD)
    {
        return TIC_MODE_STANDARD;
    }
    if (baudrate == BAUD_RATE_MODE_HISTORIQUE) 
    {
        return TIC_MODE_HISTORIQUE;
    }
    return TIC_MODE_INCONNU;
}

static void uart_detect_baudrate_task(void *pvParameters)
{
    s_toggle_event = xEventGroupCreate();
    if( !s_toggle_event)
    {
        ESP_LOGE(TAG, "fatal: xEventGroupCreate() failed");
        return;
    }

    s_toggle_timer = xTimerCreate( "baudrate_toggle_timer", TOGGLE_TIMER_DELAY_MS/portTICK_PERIOD_MS, pdFALSE, NULL, enable_toggle);
    if( !s_toggle_timer )
    {
        ESP_LOGE(TAG, "fatal; xTimerCreate() failed");
        return;
    }
    xTimerStart (s_toggle_timer, 10);
    ESP_LOGD (TAG, "toggle_timer créé");

    esp_err_t err;
    uint32_t cur_baudrate = 0;
    uint32_t new_baudrate = 0;

    for(;;)
    {
        ESP_LOGD (TAG, "detect_task: attente sur BIT_TOGGLE");
        /*bits = */xEventGroupWaitBits (s_toggle_event, (BIT_TOGGLE_REQUEST | BIT_TOGGLE_ENABLE), pdTRUE, pdTRUE, portMAX_DELAY);
        ESP_LOGD (TAG, "detect_task: TOGGLE_REQ et TOGGLE_EN were set");

        cur_baudrate = get_baudrate();

        switch (cur_baudrate)
        {
            case BAUD_RATE_MODE_HISTORIQUE:
                new_baudrate = BAUD_RATE_MODE_STANDARD;
                break;
            case BAUD_RATE_MODE_STANDARD:
                new_baudrate = BAUD_RATE_MODE_HISTORIQUE;
                break;
            default:
                new_baudrate = BAUD_RATE_MODE_STANDARD;
                ESP_LOGE( TAG, "Baud rate actuel %"PRIu32" invalide", cur_baudrate);
        }

        ESP_LOGI (TAG, "uart_set_baudrate() %"PRIu32" -> %"PRIu32, cur_baudrate, new_baudrate);
        err = uart_set_baudrate (UART_TELEINFO_NUM, new_baudrate);
        if (err != ESP_OK)
        {
            ESP_LOGE (TAG, "uart_set_baudrate() erreur %#x", err);
        }
        xTimerReset (s_toggle_timer, 10);
    }
}


static void flush_uart()
{
    uart_flush_input(UART_TELEINFO_NUM);
    xQueueReset(s_uart1_queue);
}


static void uart_rcv_task(void *pvParameters)
{
    uart_event_t event;
//    uint8_t* dtmp = calloc(1, RD_BUF_SIZE);
    tic_char_t *tmpbuf = NULL;
    int uart_err_cnt = 0;
    int length_read;

    tic_error_t err;

    for(;;) {

        // autodetection mode standard/hstorique
        //ESP_LOGD( TAG, "err_cnt=%d", err_cnt);
        if (uart_err_cnt >= MAX_ERRORS_CNT)
        {
            if( toggle_baudrate() )
            {
                uart_err_cnt = 0;
            }
        }

        //Wait for UART events.
        if(xQueueReceive(s_uart1_queue, (void *)&event, portMAX_DELAY)) {
            //memset(dtmp, 0, RD_BUF_SIZE);

            switch(event.type) {
                //Event of UART receving data
                case UART_DATA:
                    ESP_LOGD(TAG, "[UART DATA]: %d bytes", event.size);
                    
                    tmpbuf = calloc(1, event.size);   //  free() par le recepteur
                    if (!tmpbuf)
                    {
                        ESP_LOGE (TAG, "calloc() failed");
                        continue;
                    }

                    length_read = uart_read_bytes(UART_TELEINFO_NUM, tmpbuf, event.size, portMAX_DELAY);
                    err = decode_incoming_bytes (tmpbuf, length_read, get_tic_mode() );
                    if( err != TIC_OK )
                    {
                        ESP_LOGE( TAG, "%d bytes perdus", length_read);
                        free(tmpbuf);
                        tmpbuf=NULL;
                    }

                    // decremente le compteur d'erreur quand des données sont reçues
                    if ( (uart_err_cnt--) < 0 )
                    {
                        // met a jour le statut s'il n'y a plus d'erreurs
                        status_update_baudrate (get_baudrate(), 0);
                        uart_err_cnt = 0;
                    }

                    //printf("%s",dtmp);
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGI(TAG, "hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    flush_uart();
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGI(TAG, "ring buffer full");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    flush_uart();
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    //ESP_LOGI(TAG, "uart rx break");   
                    uart_err_cnt++;                        // la teleinfo n'envoie pas de BREAK donc c'est une erreur
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    //ESP_LOGI(TAG, "uart parity error");
                    uart_err_cnt++;
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    //ESP_LOGI(TAG, "uart frame error");
                    uart_err_cnt++;
                    break;
                //Others
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }
//    free(dtmp);
//    dtmp = NULL;
    vTaskDelete(NULL);
}


tic_error_t uart_task_start( )
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    esp_err_t err;
    s_uart1_queue = NULL;

    //Install UART driver, and get the queue.
    ESP_LOGD( TAG, "uart_driver_install()" );

    err = uart_driver_install(UART_TELEINFO_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, &s_uart1_queue, 0);
    if( err != ESP_OK || s_uart1_queue == NULL )
    {
        ESP_LOGE (TAG, "uart_driver_install() erreur %d", err);
        return TIC_ERR_APP_INIT;
    }

    uart_config_t cfg = {
        .baud_rate = BAUD_RATE_MODE_STANDARD,      // baudrate modifié par mode_detect_task plus tard
        .data_bits = UART_DATA_7_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    err = uart_param_config(UART_TELEINFO_NUM, &cfg);
    if( err != ESP_OK  )
    {
        ESP_LOGE( TAG, "erreur dans uart_param_config()" );
    }

    err = uart_set_pin(UART_TELEINFO_NUM, UART_PIN_NO_CHANGE, UART_TELEINFO_SIGNAL_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if( err != ESP_OK  )
    {
        ESP_LOGE( TAG, "uart_set_pin() erreur %d", err);
        return TIC_ERR_APP_INIT;
    }

    err = uart_set_rx_full_threshold (UART_TELEINFO_NUM, TIC_UART_THRESOLD);
    if( err != ESP_OK  )
    {
        ESP_LOGE (TAG, "erreur dans uart_set_rx_full_threshold() erreur %d", err);
        return TIC_ERR_APP_INIT;
    }

    if(    (xTaskCreate(uart_rcv_task, "uart_rcv_task", 4096, NULL, 12, NULL) != pdTRUE) 
        || (xTaskCreate(uart_detect_baudrate_task, "uart_detect_baudrate_task", 4096, NULL, 2, NULL) != pdTRUE ) )
    {
        ESP_LOGE( TAG, "xTaskCreate() failed" );
        return TIC_ERR_APP_INIT;
    }

    return TIC_OK;
}

