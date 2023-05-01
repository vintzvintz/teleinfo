
#include "tic_types.h"


// console.c
tic_error_t console_task_start();


// cmd_tic.c
void console_register_commands();


// cmd_nvs.c
void console_register_nvs();  



// cmd_system.c
void console_register_system(void);

// Register all system functions
//void register_system(void);

// Register common system functions: "version", "restart", "free", "heap", "tasks"
//void register_system_common(void);

// Register deep and light sleep functions
//void register_system_sleep(void);
