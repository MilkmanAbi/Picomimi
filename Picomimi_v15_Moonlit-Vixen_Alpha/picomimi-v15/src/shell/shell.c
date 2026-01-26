/**
 * PICOMIMI Shell Implementation
 * Ported from v14.3.1 "Quiet Otter" with all commands
 */
#include "shell/shell.h"
#include "api/picomimi_kernel.h"
#include "power/governor.h"
#include "kernel/scheduler.h"
#include "resource/resource.h"
#include "memory/memory.h"
#include "services/services.h"
#include "utils/utils.h"
#if PICOMIMI_SD_ENABLED
#include "fs/pmfs.h"
#endif
#if PICOMIMI_OOM_KILLER_ENABLED
#include "oom/oom.h"
#endif
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

extern pm_kernel_state_t g_kernel;
extern pm_core_scheduler_t g_core0_sched;

// Shell state
static char cmd_buffer[128];
static uint8_t cmd_pos = 0;
static char shell_cwd[64] = "/";
static bool shell_alive = true;
static bool root_mode = false;

// ============================================================================
// OUTPUT HELPERS
// ============================================================================

void pm_shell_prompt(void) {
    if (root_mode) {
        printf("picomimi:%s# ", shell_cwd);
    } else {
        printf("picomimi:%s~> ", shell_cwd);
    }
}

void pm_shell_print(const char* str) { printf("%s", str); }
void pm_shell_println(const char* str) { printf("%s\n", str); }
void pm_shell_printf(const char* fmt, ...) {
    va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args);
}

// ============================================================================
// COMMAND IMPLEMENTATIONS (from v14.3.1)
// ============================================================================

static void cmd_help(const char* args) {
    (void)args;
    printf("\n=== Picomimi-AxisOS v%s ===\n", PICOMIMI_VERSION_STRING);
    printf("\n--- System ---\n");
    printf(" help       - Show this help\n");
    printf(" ps         - List all tasks\n");
    printf(" taskinfo N - Task details\n");
    printf(" top        - System monitor\n");
    printf(" mem        - Memory stats\n");
    printf(" dmesg      - System log\n");
    printf(" uptime     - System uptime\n");
    printf(" temp       - CPU temperature\n");
    printf(" clear      - Clear screen\n");
    printf("\n--- Task Management ---\n");
    printf(" kill N     - Kill task N\n");
    printf(" root       - Toggle root mode\n");
    printf(" reboot     - Restart system\n");
    printf("\n--- CPU Governor ---\n");
    printf(" gov           - Show governor status\n");
    printf(" gov auto      - Enable auto scaling\n");
    printf(" gov manual    - Lock current profile\n");
    printf(" gov ultra     - Set ULTRA_LOW (48MHz)\n");
    printf(" gov powersave - Set POWERSAVE (96MHz)\n");
    printf(" gov balanced  - Set BALANCED (133MHz)\n");
    printf(" gov perf      - Set PERFORMANCE (200MHz)\n");
    printf(" gov turbo     - Set TURBO (260/310MHz)\n");
    printf("\n--- Filesystem (PMFS) ---\n");
    printf(" ls [path]     - List directory\n");
    printf(" cat <file>    - Display file contents\n");
    printf(" write <f> <c> - Write content to file\n");
    printf(" mkdir <dir>   - Create directory\n");
    printf(" rm <path>     - Remove file/dir (root)\n");
    printf(" cd <dir>      - Change directory\n");
    printf(" pwd           - Print working directory\n");
    printf(" tree [path]   - Show directory tree\n");
    printf(" pmfs <cmd>    - PMFS management\n");
    printf("    status/stats/fsck/format/mount/unmount\n");
    printf("\n--- Resource Management ---\n");
    printf(" res list      - Show all resource ownership\n");
    printf(" res gpio      - Show GPIO ownership\n");
    printf(" res spi       - Show SPI ownership\n");
    printf(" res i2c       - Show I2C ownership\n");
    printf(" res pwm       - Show PWM ownership\n");
    printf(" res adc       - Show ADC ownership\n");
    printf("\n--- Statistics ---\n");
    printf(" schedstat  - Scheduler stats\n");
    printf(" ipcstat    - IPC statistics\n");
    printf(" oomstat    - OOM killer stats\n");
    printf("\n--- System ---\n");
    printf(" sys        - System overview\n");
    printf(" uptime     - Show uptime\n");
    printf(" wdog       - Watchdog status\n");
    printf(" wdog feed  - Feed watchdog\n");
    printf(" wdog enable- Enable HW watchdog\n");
    printf("\n");
}

static void cmd_ps(const char* args) {
    (void)args;
    printf("\n=== System Tasks ===\n");
    printf("ID  Name             Type    State     Pri  CPU ms  Mem KB\n");
    printf("--- ---------------- ------- --------- ---- ------- ------\n");
    
    const char* state_str[] = {"READY", "RUN", "WAIT", "SUSP", "DEAD", "ZOMBI"};
    const char* type_str[] = {"KERNEL", "DRIVER", "SERVIC", "MODULE", "APP"};
    
    for (uint32_t i = 0; i < PICOMIMI_MAX_TASKS; i++) {
        pm_tcb_t* task = &g_kernel.tasks[i];
        if (task->id == PM_INVALID_TASK) continue;
        
        uint8_t type_idx = task->task_type;
        if (type_idx > 4) type_idx = 4;
        
        uint8_t state_idx = task->state;
        if (state_idx > 5) state_idx = 5;
        
        printf("%-3lu %-16s %-7s %-9s %4d %7lu %6lu\n",
               (unsigned long)task->id,
               task->name,
               type_str[type_idx],
               task->mem_blocked ? "BLCKD" : state_str[state_idx],
               task->priority,
               (unsigned long)task->cpu_time_ms,
               (unsigned long)(task->mem_used / 1024));
    }
    
    printf("\n--- Summary ---\n");
    printf("Tasks: %lu active\n", (unsigned long)g_kernel.task_count);
    printf("Context switches: %lu\n", (unsigned long)g_core0_sched.level_mask);
}

static void cmd_taskinfo(const char* args) {
    if (!args || strlen(args) == 0) {
        printf("Usage: taskinfo <task_id>\n");
        return;
    }
    
    int id = atoi(args);
    if (id < 0 || id >= PICOMIMI_MAX_TASKS) {
        printf("Invalid task ID\n");
        return;
    }
    
    pm_tcb_t* task = &g_kernel.tasks[id];
    if (task->id == PM_INVALID_TASK) {
        printf("Task %d not found\n", id);
        return;
    }
    
    const char* state_str[] = {"READY", "RUNNING", "WAITING", "SUSPENDED", "TERMINATED", "ZOMBIE"};
    const char* type_str[] = {"KERNEL", "DRIVER", "SERVICE", "MODULE", "APP"};
    
    printf("\n=== Task Information ===\n");
    printf("ID: %lu\n", (unsigned long)task->id);
    printf("Name: %s\n", task->name);
    printf("Type: %s\n", type_str[task->task_type < 5 ? task->task_type : 4]);
    printf("Priority: %d\n", task->priority);
    printf("State: %s\n", task->mem_blocked ? "BLOCKED" : state_str[task->state < 6 ? task->state : 5]);
    printf("\nMemory Used: %lu bytes\n", (unsigned long)task->mem_used);
    printf("Memory Peak: %lu bytes\n", (unsigned long)task->mem_peak);
    printf("CPU Time: %lu ms\n", (unsigned long)task->cpu_time_ms);
    printf("Total CPU: %lu ms\n", (unsigned long)task->total_cpu_time_ms);
    printf("Started: %lu ms after boot\n", (unsigned long)task->start_time_ms);
    
    if (task->flags & PICOMIMI_TASK_FLAG_PROTECTED) {
        printf("\n[PROTECTED TASK]\n");
    }
}

static void cmd_top(const char* args) {
    (void)args;
    printf("\n=== System Monitor ===\n");
    printf("Uptime: %lu s\n", (unsigned long)(g_kernel.uptime_ms / 1000));
    printf("CPU Temperature: %.1f C\n", g_kernel.governor.temperature);
    printf("CPU Frequency: %lu MHz (%s)\n", 
           (unsigned long)(g_kernel.governor.current_freq_khz / 1000),
           pm_governor_profile_name(g_kernel.governor.current_profile));
    printf("Memory: %lu / %lu KB used\n",
           (unsigned long)(g_kernel.used_memory / 1024),
           (unsigned long)(PICOMIMI_HEAP_SIZE / 1024));
    printf("Free Memory: %lu KB\n", (unsigned long)(g_kernel.free_memory / 1024));
    printf("Tasks: %lu\n", (unsigned long)g_kernel.task_count);
    printf("Context Switches: %lu\n", (unsigned long)g_core0_sched.level_mask);
    
    if (g_kernel.governor.thermal_throttled) {
        printf("\n*** THERMAL THROTTLING ACTIVE ***\n");
    }
}

static void cmd_mem(const char* args) {
    (void)args;
    printf("\n=== Memory Statistics ===\n");
    printf("Total Heap: %lu KB\n", (unsigned long)(PICOMIMI_HEAP_SIZE / 1024));
    printf("Used: %lu KB\n", (unsigned long)(g_kernel.used_memory / 1024));
    printf("Free: %lu KB\n", (unsigned long)(g_kernel.free_memory / 1024));
    printf("Peak Usage: %lu KB\n", (unsigned long)(g_kernel.mem_stats.peak_usage / 1024));
    printf("\nAllocations: %lu\n", (unsigned long)g_kernel.mem_stats.total_allocs);
    printf("Frees: %lu\n", (unsigned long)g_kernel.mem_stats.total_frees);
    
    pm_mem_pressure_t pressure = pm_get_memory_pressure();
    const char* pressure_names[] = {"NONE", "LOW", "MODERATE", "HIGH", "CRITICAL", "EMERGENCY"};
    printf("Memory Pressure: %s\n", pressure_names[pressure < 6 ? pressure : 5]);
    
    if (pressure >= MEM_PRESSURE_HIGH) {
        printf("\n*** LOW MEMORY WARNING ***\n");
    }
}

static void cmd_dmesg(const char* args) {
    (void)args;
    printf("\n=== System Log ===\n");
    printf("(Ring buffer logging not yet implemented in v15)\n");
    printf("Use serial output for real-time logs.\n");
}

static void cmd_uptime(const char* args) {
    (void)args;
    uint32_t uptime = g_kernel.uptime_ms;
    uint32_t days = uptime / 86400000;
    uint32_t hours = (uptime % 86400000) / 3600000;
    uint32_t mins = (uptime % 3600000) / 60000;
    uint32_t secs = (uptime % 60000) / 1000;
    
    printf("Uptime: ");
    if (days > 0) printf("%lud ", (unsigned long)days);
    printf("%02lu:%02lu:%02lu\n", (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    printf("Boot time: %lu ms to ready\n", (unsigned long)(g_kernel.uptime_ms > 0 ? g_kernel.tasks[0].start_time_ms : 0));
}

static void cmd_temp(const char* args) {
    (void)args;
    float temp = pm_governor_read_temp(&g_kernel.governor);
    printf("CPU Temperature: %.1f C\n", temp);
    printf("Peak Temperature: %.1f C\n", g_kernel.governor.temperature_peak);
    printf("Thermal Limit: %d C\n", PICOMIMI_THERMAL_LIMIT);
    
    if (g_kernel.governor.thermal_throttled) {
        printf("\n*** THERMAL THROTTLING ACTIVE ***\n");
        printf("Throttle count: %lu\n", (unsigned long)g_kernel.governor.throttle_count);
    }
}

static void cmd_clear(const char* args) {
    (void)args;
    printf("\033[2J\033[H");  // ANSI clear screen
}

static void cmd_kill(const char* args) {
    if (!args || strlen(args) == 0) {
        printf("Usage: kill <task_id>\n");
        return;
    }
    
    int id = atoi(args);
    if (id < 0 || id >= PICOMIMI_MAX_TASKS) {
        printf("Invalid task ID\n");
        return;
    }
    
    pm_tcb_t* task = &g_kernel.tasks[id];
    if (task->id == PM_INVALID_TASK) {
        printf("Task %d not found\n", id);
        return;
    }
    
    if (task->flags & PICOMIMI_TASK_FLAG_PROTECTED) {
        if (!root_mode) {
            printf("Cannot kill protected task '%s' (use 'root' first)\n", task->name);
            return;
        }
        printf("WARNING: Killing protected task!\n");
    }
    
    pm_result_t result = pm_task_terminate(id);
    if (result == PM_OK) {
        printf("Task %d '%s' terminated\n", id, task->name);
    } else if (result == PM_ERROR_DENIED) {
        printf("Permission denied\n");
    } else {
        printf("Failed to terminate task %d\n", id);
    }
}

static void cmd_root(const char* args) {
    (void)args;
    root_mode = !root_mode;
    if (root_mode) {
        printf("Root mode ENABLED - be careful!\n");
    } else {
        printf("Root mode disabled\n");
    }
}

static void cmd_reboot(const char* args) {
    (void)args;
    printf("Rebooting...\n");
    sleep_ms(100);
    watchdog_reboot(0, 0, 0);
    while (1) tight_loop_contents();
}

static void cmd_gov(const char* args) {
    pm_governor_state_t* gov = &g_kernel.governor;
    
    if (!args || strlen(args) == 0) {
        // Show status
        printf("\n=== CPU Governor v2.0 ===\n");
        printf("Mode: %s\n", gov->mode == GOV_MODE_ONDEMAND ? "AUTO" : "MANUAL");
        printf("Profile: %s\n", pm_governor_profile_name(gov->current_profile));
        printf("Frequency: %lu MHz\n", (unsigned long)(gov->current_freq_khz / 1000));
        printf("Temperature: %.1f C (peak: %.1f C)\n", gov->temperature, gov->temperature_peak);
        printf("CPU Load: %d%% (avg: %d%%)\n", gov->cpu_load, gov->cpu_load_avg);
        printf("Thermal Throttled: %s\n", gov->thermal_throttled ? "YES" : "NO");
        printf("Transitions: %lu\n", (unsigned long)gov->transition_count);
        printf("Thermal Throttles: %lu\n", (unsigned long)gov->throttle_count);
        printf("\nProfiles: ultra, powersave, balanced, perf, turbo\n");
        printf("Commands: gov <profile>, gov auto, gov manual\n");
        return;
    }
    
    // Parse commands
    if (strcmp(args, "auto") == 0) {
        pm_governor_set_mode(gov, GOV_MODE_ONDEMAND);
        printf("Governor: AUTO mode enabled\n");
    } else if (strcmp(args, "manual") == 0) {
        pm_governor_set_mode(gov, GOV_MODE_MANUAL);
        printf("Governor: MANUAL mode (locked at %s)\n", pm_governor_profile_name(gov->current_profile));
    } else if (strcmp(args, "ultra") == 0 || strcmp(args, "ultralow") == 0) {
        pm_governor_set_mode(gov, GOV_MODE_MANUAL);
        pm_governor_set_profile(gov, CPU_PROFILE_ULTRA_LOW);
        printf("Profile: ULTRA_LOW (48 MHz)\n");
    } else if (strcmp(args, "powersave") == 0 || strcmp(args, "save") == 0) {
        pm_governor_set_mode(gov, GOV_MODE_MANUAL);
        pm_governor_set_profile(gov, CPU_PROFILE_POWERSAVE);
        printf("Profile: POWERSAVE (96 MHz)\n");
    } else if (strcmp(args, "balanced") == 0 || strcmp(args, "bal") == 0) {
        pm_governor_set_mode(gov, GOV_MODE_MANUAL);
        pm_governor_set_profile(gov, CPU_PROFILE_BALANCED);
        printf("Profile: BALANCED (133 MHz)\n");
    } else if (strcmp(args, "performance") == 0 || strcmp(args, "perf") == 0) {
        pm_governor_set_mode(gov, GOV_MODE_MANUAL);
        pm_governor_set_profile(gov, CPU_PROFILE_PERFORMANCE);
        printf("Profile: PERFORMANCE (200 MHz)\n");
    } else if (strcmp(args, "turbo") == 0) {
        pm_governor_set_mode(gov, GOV_MODE_MANUAL);
        pm_governor_set_profile(gov, CPU_PROFILE_TURBO);
        printf("Profile: TURBO (%lu MHz)\n", (unsigned long)(PICOMIMI_FREQ_TURBO / 1000000));
    } else {
        printf("Unknown governor command: %s\n", args);
        printf("Valid: auto, manual, ultra, powersave, balanced, perf, turbo\n");
    }
}

static void cmd_schedstat(const char* args) {
    (void)args;
    printf("\n=== Scheduler Statistics ===\n");
    printf("Algorithm: Priority Round-Robin\n");
    printf("Tick interval: %d us\n", PICOMIMI_SCHED_TICK_US);
    printf("Context Switches: %lu\n", (unsigned long)g_core0_sched.level_mask);
    printf("Preemptions: %lu\n", (unsigned long)g_core0_sched.preemptions);
    printf("Current Task: %lu\n", (unsigned long)g_kernel.current_task);
}

static void cmd_ipcstat(const char* args) {
    (void)args;
    printf("\n=== IPC Statistics ===\n");
    printf("(IPC system not yet implemented in v15)\n");
}

// ============================================================================
// FILESYSTEM COMMANDS
// ============================================================================

static void cmd_ls(const char* args) {
    const char* path = (args && *args) ? args : shell_cwd;
    
    printf("\nDirectory: %s\n", path);
    printf("----------------------------------------\n");
    
#if PICOMIMI_SD_ENABLED
    pmfs_dir_t dir;
    if (pmfs_opendir(path, &dir) == PMFS_OK) {
        pmfs_dirent_t entry;
        while (pmfs_readdir(&dir, &entry) == PMFS_OK) {
            if (entry.is_dir) {
                printf("[DIR]  %s/\n", entry.name);
            } else {
                printf("       %-20s  %lu bytes\n", entry.name, (unsigned long)entry.size);
            }
        }
        pmfs_closedir(&dir);
    } else {
        printf("Cannot open directory: %s\n", path);
    }
#else
    printf("Filesystem not enabled (no SD card)\n");
#endif
}

static void cmd_cat(const char* args) {
    if (!args || !*args) {
        printf("Usage: cat <filename>\n");
        return;
    }
    
#if PICOMIMI_SD_ENABLED
    pmfs_file_t file;
    if (pmfs_open(args, PMFS_READ, &file) == PMFS_OK) {
        char buf[64];
        size_t read;
        while ((read = pmfs_read(&file, buf, sizeof(buf) - 1)) > 0) {
            buf[read] = '\0';
            printf("%s", buf);
        }
        printf("\n");
        pmfs_close(&file);
    } else {
        printf("Cannot open file: %s\n", args);
    }
#else
    printf("Filesystem not enabled\n");
#endif
}

static void cmd_write(const char* args) {
    if (!args || !*args) {
        printf("Usage: write <filename> <content>\n");
        return;
    }
    
    char filename[64];
    const char* space = strchr(args, ' ');
    if (!space) {
        printf("Usage: write <filename> <content>\n");
        return;
    }
    
    size_t len = space - args;
    if (len >= sizeof(filename)) len = sizeof(filename) - 1;
    strncpy(filename, args, len);
    filename[len] = '\0';
    
    const char* content = space + 1;
    while (*content == ' ') content++;
    
#if PICOMIMI_SD_ENABLED
    pmfs_file_t file;
    if (pmfs_open(filename, PMFS_WRITE | PMFS_CREATE, &file) == PMFS_OK) {
        pmfs_write(&file, content, strlen(content));
        pmfs_close(&file);
        printf("Wrote %lu bytes to %s\n", (unsigned long)strlen(content), filename);
    } else {
        printf("Cannot create file: %s\n", filename);
    }
#else
    printf("Filesystem not enabled\n");
#endif
}

static void cmd_mkdir(const char* args) {
    if (!args || !*args) {
        printf("Usage: mkdir <dirname>\n");
        return;
    }
    
#if PICOMIMI_SD_ENABLED
    if (pmfs_mkdir(args) == PMFS_OK) {
        printf("Created directory: %s\n", args);
    } else {
        printf("Cannot create directory: %s\n", args);
    }
#else
    printf("Filesystem not enabled\n");
#endif
}

static void cmd_rm(const char* args) {
    if (!args || !*args) {
        printf("Usage: rm <file/dir>\n");
        return;
    }
    
    if (!root_mode) {
        printf("Permission denied. Use 'root' to enable.\n");
        return;
    }
    
#if PICOMIMI_SD_ENABLED
    if (pmfs_remove(args) == PMFS_OK) {
        printf("Removed: %s\n", args);
    } else {
        printf("Cannot remove: %s\n", args);
    }
#else
    printf("Filesystem not enabled\n");
#endif
}

static void cmd_cd(const char* args) {
    if (!args || !*args || strcmp(args, "~") == 0) {
        strcpy(shell_cwd, "/");
        return;
    }
    
    if (strcmp(args, "..") == 0) {
        // Go up one level
        char* last_slash = strrchr(shell_cwd, '/');
        if (last_slash && last_slash != shell_cwd) {
            *last_slash = '\0';
        } else {
            strcpy(shell_cwd, "/");
        }
        return;
    }
    
    // Set new path
    if (args[0] == '/') {
        strncpy(shell_cwd, args, sizeof(shell_cwd) - 1);
    } else {
        // Relative path
        size_t len = strlen(shell_cwd);
        if (len > 0 && shell_cwd[len-1] != '/') {
            strncat(shell_cwd, "/", sizeof(shell_cwd) - len - 1);
        }
        strncat(shell_cwd, args, sizeof(shell_cwd) - strlen(shell_cwd) - 1);
    }
}

static void cmd_pwd(const char* args) {
    (void)args;
    printf("%s\n", shell_cwd);
}

static void cmd_tree(const char* args) {
    const char* path = (args && *args) ? args : shell_cwd;
    printf("\nTree: %s\n", path);
    
#if PICOMIMI_SD_ENABLED
    pmfs_print_tree(path, 0);
#else
    printf("Filesystem not enabled\n");
#endif
}

static void cmd_pmfs(const char* args) {
    if (!args || !*args) {
        printf("Usage: pmfs <status|stats|fsck|format|mount|unmount>\n");
        return;
    }
    
#if PICOMIMI_SD_ENABLED
    if (strcmp(args, "status") == 0 || strcmp(args, "stat") == 0) {
        pmfs_print_stats();
    } else if (strcmp(args, "stats") == 0) {
        pmfs_stats_t stats;
        pmfs_get_stats(&stats);
        printf("=== PMFS Statistics ===\n");
        printf("Total:      %lu KB\n", (unsigned long)(stats.total_bytes / 1024));
        printf("Used:       %lu KB\n", (unsigned long)(stats.used_bytes / 1024));
        printf("Free:       %lu KB\n", (unsigned long)(stats.free_bytes / 1024));
        printf("Files:      %u\n", stats.file_count);
        printf("Dirs:       %u\n", stats.dir_count);
        printf("Writes:     %lu\n", stats.write_count);
        printf("Reads:      %lu\n", stats.read_count);
    } else if (strcmp(args, "fsck") == 0) {
        if (!root_mode) {
            printf("Permission denied. Use 'root' first.\n");
            return;
        }
        printf("Running filesystem check...\n");
        if (pmfs_fsck() == PMFS_OK) {
            printf("Filesystem OK\n");
        } else {
            printf("Filesystem errors found\n");
        }
    } else if (strcmp(args, "format") == 0) {
        if (!root_mode) {
            printf("Permission denied. Use 'root' first.\n");
            return;
        }
        printf("Formatting PMFS...\n");
        if (pmfs_format_and_initialize() == PMFS_OK) {
            printf("Format complete\n");
        } else {
            printf("Format failed\n");
        }
    } else if (strcmp(args, "mount") == 0) {
        if (pmfs_mount() == PMFS_OK) {
            printf("Mounted\n");
        } else {
            printf("Mount failed\n");
        }
    } else if (strcmp(args, "unmount") == 0) {
        pmfs_unmount();
        printf("Unmounted\n");
    } else {
        printf("Unknown pmfs command: %s\n", args);
    }
#else
    printf("Filesystem not enabled\n");
#endif
}

// ============================================================================
// RESOURCE MANAGEMENT COMMANDS
// ============================================================================

static void cmd_res(const char* args) {
    if (!args || !*args) {
        printf("Usage: res <list|gpio|spi|i2c|pwm|adc|claim|release>\n");
        return;
    }
    
    if (strcmp(args, "list") == 0) {
        printf("\n=== Resource Ownership ===\n");
        pm_resource_print_all();
    } else if (strcmp(args, "gpio") == 0) {
        printf("\n=== GPIO Ownership ===\n");
        for (int i = 0; i < 30; i++) {
            pm_task_id_t owner = pm_gpio_get_owner(i);
            if (owner != PM_INVALID_TASK) {
                printf("GPIO %2d: Task %u (%s)\n", i, owner, 
                       g_kernel.tasks[owner].name);
            }
        }
    } else if (strcmp(args, "spi") == 0) {
        printf("\n=== SPI Ownership ===\n");
        for (int i = 0; i < 2; i++) {
            pm_task_id_t owner = pm_spi_get_owner(i);
            if (owner != PM_INVALID_TASK) {
                printf("SPI%d: Task %u (%s)\n", i, owner,
                       g_kernel.tasks[owner].name);
            } else {
                printf("SPI%d: (free)\n", i);
            }
        }
    } else if (strcmp(args, "i2c") == 0) {
        printf("\n=== I2C Ownership ===\n");
        for (int i = 0; i < 2; i++) {
            pm_task_id_t owner = pm_i2c_get_owner(i);
            if (owner != PM_INVALID_TASK) {
                printf("I2C%d: Task %u (%s)\n", i, owner,
                       g_kernel.tasks[owner].name);
            } else {
                printf("I2C%d: (free)\n", i);
            }
        }
    } else if (strcmp(args, "pwm") == 0) {
        printf("\n=== PWM Ownership ===\n");
        for (int i = 0; i < 8; i++) {
            pm_task_id_t owner = pm_pwm_get_owner(i);
            if (owner != PM_INVALID_TASK) {
                printf("PWM Slice %d: Task %u (%s)\n", i, owner,
                       g_kernel.tasks[owner].name);
            }
        }
    } else if (strcmp(args, "adc") == 0) {
        printf("\n=== ADC Ownership ===\n");
        for (int i = 0; i < 5; i++) {
            pm_task_id_t owner = pm_adc_get_owner(i);
            if (owner != PM_INVALID_TASK) {
                printf("ADC Channel %d: Task %u (%s)\n", i, owner,
                       g_kernel.tasks[owner].name);
            } else {
                printf("ADC Channel %d: (free)\n", i);
            }
        }
    } else {
        printf("Unknown res command: %s\n", args);
    }
}

static void cmd_oomstat(const char* args) {
    (void)args;
#if PICOMIMI_OOM_KILLER_ENABLED
    pm_oom_print_stats();
#else
    printf("OOM killer not enabled\n");
#endif
}

// ============================================================================
// SYSTEM COMMANDS
// ============================================================================

static void cmd_sys(const char* args) {
    (void)args;
    
    printf("\n=== System Information ===\n");
    
    // Uptime
    uint32_t uptime_s = pm_time_ms() / 1000;
    uint32_t days = uptime_s / 86400;
    uint32_t hours = (uptime_s % 86400) / 3600;
    uint32_t mins = (uptime_s % 3600) / 60;
    uint32_t secs = uptime_s % 60;
    
    printf("Uptime:      ");
    if (days > 0) printf("%lud ", (unsigned long)days);
    printf("%02lu:%02lu:%02lu\n", (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    
    // CPU info
    printf("CPU:         RP2040/RP2350\n");
    printf("Frequency:   %lu MHz\n", (unsigned long)(g_kernel.governor.current_freq_khz / 1000));
    printf("CPU Load:    %.1f%%\n", g_kernel.cpu_usage);
    printf("Temperature: %.1f C\n", g_kernel.governor.temperature);
    
    // Memory info
    pm_mem_stats_t mem_stats;
    pm_mem_get_stats(&mem_stats);
    printf("Memory:      %lu / %lu KB (%.1f%% used)\n",
           (unsigned long)(mem_stats.total_used / 1024),
           (unsigned long)(mem_stats.total_size / 1024),
           (float)mem_stats.total_used * 100.0f / mem_stats.total_size);
    
    // Task info
    uint32_t active_tasks = 0;
    for (uint32_t i = 0; i < PICOMIMI_MAX_TASKS; i++) {
        if (g_kernel.tasks[i].id != PM_INVALID_TASK &&
            g_kernel.tasks[i].state != TASK_STATE_TERMINATED) {
            active_tasks++;
        }
    }
    printf("Tasks:       %lu active\n", (unsigned long)active_tasks);
    
    // Governor
    printf("Governor:    %s (%s)\n",
           pm_governor_profile_name(g_kernel.governor.current_profile),
           g_kernel.governor.mode == GOV_MODE_ONDEMAND ? "auto" : "manual");
    
    // Scheduler stats
    printf("Ctx switches: %lu\n", (unsigned long)g_core0_sched.context_switches);
    printf("Preemptions:  %lu\n", (unsigned long)g_core0_sched.preemptions);
    
    printf("\n");
}

static void cmd_wdog(const char* args) {
    if (!args || !*args) {
        // Show watchdog status
        const pm_watchdog_state_t* wdog = pm_watchdog_get_state();
        
        printf("\n=== Watchdog Status ===\n");
        printf("Enabled:        %s\n", wdog->enabled ? "yes" : "no");
        printf("HW Watchdog:    %s\n", wdog->hw_enabled ? "enabled" : "disabled");
        printf("Task Monitor:   %s\n", wdog->task_watchdog_enabled ? "enabled" : "disabled");
        printf("Feed Count:     %lu\n", (unsigned long)wdog->feed_count);
        printf("Timeout:        %lu ms\n", (unsigned long)wdog->timeout_ms);
        
        uint32_t since_feed = pm_time_ms() - wdog->last_feed_ms;
        printf("Last Fed:       %lu ms ago\n", (unsigned long)since_feed);
        
        if (wdog->task_watchdog_violations > 0) {
            printf("Task Violations: %lu\n", (unsigned long)wdog->task_watchdog_violations);
        }
        printf("\n");
        return;
    }
    
    if (strcmp(args, "feed") == 0) {
        pm_watchdog_feed();
        printf("Watchdog fed\n");
    } else if (strcmp(args, "enable") == 0) {
        pm_watchdog_enable(true);
        printf("Hardware watchdog enabled (cannot be disabled)\n");
    } else if (strcmp(args, "taskmon") == 0) {
        pm_watchdog_enable_task_monitor(true);
        printf("Task monitoring enabled\n");
    } else if (strcmp(args, "notaskmon") == 0) {
        pm_watchdog_enable_task_monitor(false);
        printf("Task monitoring disabled\n");
    } else {
        printf("Usage: wdog [feed|enable|taskmon|notaskmon]\n");
    }
}

// ============================================================================
// COMMAND DISPATCH
// ============================================================================

static void shell_execute(char* cmd) {
    if (strlen(cmd) == 0) return;
    
    // Split command and arguments
    char* args = strchr(cmd, ' ');
    if (args) {
        *args = '\0';
        args++;
        while (*args == ' ') args++;
    }
    
    // Built-in commands
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) { cmd_help(args); return; }
    if (strcmp(cmd, "ps") == 0) { cmd_ps(args); return; }
    if (strcmp(cmd, "taskinfo") == 0) { cmd_taskinfo(args); return; }
    if (strcmp(cmd, "top") == 0) { cmd_top(args); return; }
    if (strcmp(cmd, "mem") == 0) { cmd_mem(args); return; }
    if (strcmp(cmd, "dmesg") == 0) { cmd_dmesg(args); return; }
    if (strcmp(cmd, "uptime") == 0) { cmd_uptime(args); return; }
    if (strcmp(cmd, "temp") == 0) { cmd_temp(args); return; }
    if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) { cmd_clear(args); return; }
    if (strcmp(cmd, "kill") == 0) { cmd_kill(args); return; }
    if (strcmp(cmd, "root") == 0) { cmd_root(args); return; }
    if (strcmp(cmd, "reboot") == 0) { cmd_reboot(args); return; }
    if (strcmp(cmd, "gov") == 0) { cmd_gov(args); return; }
    if (strcmp(cmd, "schedstat") == 0) { cmd_schedstat(args); return; }
    if (strcmp(cmd, "ipcstat") == 0) { cmd_ipcstat(args); return; }
    
    // Filesystem commands
    if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0) { cmd_ls(args); return; }
    if (strcmp(cmd, "cat") == 0 || strcmp(cmd, "type") == 0) { cmd_cat(args); return; }
    if (strcmp(cmd, "write") == 0 || strcmp(cmd, "echo") == 0) { cmd_write(args); return; }
    if (strcmp(cmd, "mkdir") == 0) { cmd_mkdir(args); return; }
    if (strcmp(cmd, "rm") == 0 || strcmp(cmd, "del") == 0) { cmd_rm(args); return; }
    if (strcmp(cmd, "cd") == 0) { cmd_cd(args); return; }
    if (strcmp(cmd, "pwd") == 0) { cmd_pwd(args); return; }
    if (strcmp(cmd, "tree") == 0) { cmd_tree(args); return; }
    if (strcmp(cmd, "pmfs") == 0) { cmd_pmfs(args); return; }
    
    // Resource commands
    if (strcmp(cmd, "res") == 0) { cmd_res(args); return; }
    if (strcmp(cmd, "oomstat") == 0) { cmd_oomstat(args); return; }
    
    // System commands
    if (strcmp(cmd, "sys") == 0) { cmd_sys(args); return; }
    if (strcmp(cmd, "uptime") == 0) { cmd_uptime(args); return; }
    if (strcmp(cmd, "wdog") == 0) { cmd_wdog(args); return; }
    
    printf("Unknown command: %s\n", cmd);
    printf("Type 'help' for available commands\n");
}

// ============================================================================
// SHELL TASK
// ============================================================================

void pm_shell_task(void* arg) {
    (void)arg;
    
    if (!shell_alive) {
        pm_task_sleep(10000);
        return;
    }
    
    // Process serial input
    int c = getchar_timeout_us(0);
    while (c != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            printf("\n");
            
            char temp_cmd[128];
            strncpy(temp_cmd, cmd_buffer, sizeof(temp_cmd) - 1);
            temp_cmd[sizeof(temp_cmd) - 1] = '\0';
            
            shell_execute(temp_cmd);
            
            cmd_pos = 0;
            memset(cmd_buffer, 0, sizeof(cmd_buffer));
            
            pm_shell_prompt();
        } else if (c == '\b' || c == 127) {  // Backspace
            if (cmd_pos > 0) {
                cmd_pos--;
                cmd_buffer[cmd_pos] = '\0';
                printf("\b \b");
            }
        } else if (c == 3) {  // Ctrl+C
            printf("^C\n");
            cmd_pos = 0;
            memset(cmd_buffer, 0, sizeof(cmd_buffer));
            pm_shell_prompt();
        } else if (cmd_pos < sizeof(cmd_buffer) - 1 && c >= 32 && c < 127) {
            cmd_buffer[cmd_pos++] = (char)c;
            putchar(c);
        }
        
        c = getchar_timeout_us(0);
    }
    
    pm_task_sleep(10);  // Check for input every 10ms
}

void pm_shell_init(pm_task_id_t id) {
    (void)id;
    shell_alive = true;
    cmd_pos = 0;
    memset(cmd_buffer, 0, sizeof(cmd_buffer));
    strcpy(shell_cwd, "/");
    root_mode = false;
    pm_klog(PICOMIMI_LOG_LEVEL_INFO, "Shell initialized");
}

void pm_shell_deinit(void) {
    shell_alive = false;
    pm_klog(PICOMIMI_LOG_LEVEL_INFO, "Shell deinitialized");
}

// Shell callbacks
static pm_module_callbacks_t shell_callbacks = {
    .init = pm_shell_init,
    .tick = pm_shell_task,
    .deinit = pm_shell_deinit,
    .suspend = NULL,
    .resume = NULL,
    .on_message = NULL
};

pm_module_callbacks_t* pm_shell_get_callbacks(void) {
    return &shell_callbacks;
}

// Command registration (for extensibility)
pm_result_t pm_shell_register_cmd(const char* name, pm_shell_cmd_handler_t handler, const char* help) {
    (void)name; (void)handler; (void)help;
    // TODO: Implement dynamic command registration
    return PM_OK;
}
