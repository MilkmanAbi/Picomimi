/**
 * PICOMIMI Shell Header
 */
#ifndef PICOMIMI_SHELL_H
#define PICOMIMI_SHELL_H

#include "config/picomimi_config.h"
#include "api/picomimi_types.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// Command handler type
typedef void (*pm_shell_cmd_handler_t)(const char* args);

// Shell command entry
typedef struct {
    char name[16];
    pm_shell_cmd_handler_t handler;
    const char* help;
} pm_shell_cmd_t;

// Shell task functions
void pm_shell_task(void* arg);
void pm_shell_init(pm_task_id_t id);
void pm_shell_deinit(void);

// Get shell callbacks for task creation
pm_module_callbacks_t* pm_shell_get_callbacks(void);

// Output functions
void pm_shell_prompt(void);
void pm_shell_print(const char* str);
void pm_shell_println(const char* str);
void pm_shell_printf(const char* fmt, ...);

// Register custom command
pm_result_t pm_shell_register_cmd(const char* name, pm_shell_cmd_handler_t handler, const char* help);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_SHELL_H
