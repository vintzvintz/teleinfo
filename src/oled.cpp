
/*

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef SDL_EMULATION
#include "sdl_core.h"
#endif

#include "lcdgfx.h"

extern void setup(void);
extern void loop(void);

#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

void main_task(void *args)
{
    setup();
    for(;;) {
        loop();
#ifdef SDL_EMULATION
        sdl_read_analog(0);
#endif
    }
}

extern "C" void app_main(void)
{
    xTaskCreatePinnedToCore(main_task, "mainTask", 8192, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
}
*/