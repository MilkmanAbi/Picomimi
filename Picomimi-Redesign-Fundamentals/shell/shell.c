/**
 * Picomimi Shell
 * 
 * Interactive command shell for debugging and control.
 */

#include "picomimi.h"

// ============================================================================
// SHELL STATE
// ============================================================================

#define SHELL_MAX_LINE      64
#define SHELL_MAX_ARGS      8
#define SHELL_HISTORY_SIZE  4

static char line_buffer[SHELL_MAX_LINE];
static int line_pos = 0;
static char history[SHELL_HISTORY_SIZE][SHELL_MAX_LINE];
static int history_count = 0;
static int history_pos = 0;

// ============================================================================
// STRING HELPERS
// ============================================================================

static int strlen(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

static void strcpy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}

static void print_num(u32 n) {
    char buf[12];
    int i = 0;
    if (n == 0) {
        shell_putc('0');
        return;
    }
    while (n) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i--) shell_putc(buf[i]);
}

static void print_hex(u32 n) {
    shell_puts("0x");
    for (int i = 7; i >= 0; i--) {
        int d = (n >> (i * 4)) & 0xF;
        shell_putc(d < 10 ? '0' + d : 'a' + d - 10);
    }
}

// ============================================================================
// I/O WRAPPERS
// ============================================================================

void shell_putc(char c) {
    uart_putc(c);
}

void shell_puts(const char *s) {
    while (*s) shell_putc(*s++);
}

int shell_getc(void) {
    return uart_getc();
}

// ============================================================================
// COMMANDS
// ============================================================================

static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_puts("\n");
    shell_puts("=== Picomimi Shell v" PICOMIMI_VERSION " ===\n\n");
    shell_puts("Commands:\n");
    shell_puts("  help          - Show this help\n");
    shell_puts("  ps            - List tasks\n");
    shell_puts("  mem           - Memory stats\n");
    shell_puts("  sched         - Scheduler info\n");
    shell_puts("  domains       - List scheduler domains\n");
    shell_puts("  top           - Live task monitor\n");
    shell_puts("  gpio <pin> [val] - Read/write GPIO\n");
    shell_puts("  led [on|off]  - Control LED\n");
    shell_puts("  temp          - Read temperature sensor\n");
    shell_puts("  uptime        - System uptime\n");
    shell_puts("  free          - Free memory\n");
    shell_puts("  reboot        - Soft reboot\n");
    shell_puts("\n");
}

static void cmd_ps(int argc, char **argv) {
    (void)argc; (void)argv;
    extern void task_list(void);
    task_list();
}

static void cmd_mem(int argc, char **argv) {
    (void)argc; (void)argv;
    extern void mem_stats(void *);
    
    typedef struct { u32 total, used, free, largest; } stats_t;
    stats_t stats;
    mem_stats(&stats);
    
    shell_puts("\nMemory:\n");
    shell_puts("  Total:   "); print_num(stats.total); shell_puts(" bytes\n");
    shell_puts("  Used:    "); print_num(stats.used); shell_puts(" bytes\n");
    shell_puts("  Free:    "); print_num(stats.free); shell_puts(" bytes\n");
    shell_puts("  Largest: "); print_num(stats.largest); shell_puts(" bytes\n\n");
}

static void cmd_sched(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_puts("\nScheduler Classes:\n");
    shell_puts("  - cooperative (fibers/coroutines)\n");
    shell_puts("  - realtime (EDF deadline)\n");
    shell_puts("  - fair (CFS-like)\n");
    shell_puts("  - batch (throughput)\n");
    shell_puts("  - idle (background)\n");
    shell_puts("\nActive Domains:\n");
    shell_puts("  ID  Name        Class       Priority  Tasks\n");
    shell_puts("  --  ----        -----       --------  -----\n");
    // Would iterate domains here
    shell_puts("   0  realtime    EDF            0        0\n");
    shell_puts("   1  normal      fair          10        2\n");
    shell_puts("   2  batch       batch         20        0\n");
    shell_puts("   3  idle        idle         255        1\n");
    shell_puts("\n");
}

static void cmd_domains(int argc, char **argv) {
    cmd_sched(argc, argv);
}

static void cmd_gpio(int argc, char **argv) {
    if (argc < 2) {
        shell_puts("Usage: gpio <pin> [value]\n");
        return;
    }
    
    u32 pin = 0;
    for (char *p = argv[1]; *p; p++) {
        pin = pin * 10 + (*p - '0');
    }
    
    if (argc >= 3) {
        bool val = (argv[2][0] == '1');
        gpio_set(pin, val);
        shell_puts("GPIO "); print_num(pin);
        shell_puts(" = "); shell_puts(val ? "HIGH\n" : "LOW\n");
    } else {
        bool val = gpio_get(pin);
        shell_puts("GPIO "); print_num(pin);
        shell_puts(" = "); shell_puts(val ? "HIGH\n" : "LOW\n");
    }
}

static void cmd_led(int argc, char **argv) {
    #define LED_PIN 25  // RP2040 onboard LED
    
    if (argc < 2) {
        bool val = gpio_get(LED_PIN);
        shell_puts("LED is "); shell_puts(val ? "ON\n" : "OFF\n");
        return;
    }
    
    bool on = (strcmp(argv[1], "on") == 0 || argv[1][0] == '1');
    gpio_set(LED_PIN, on);
    shell_puts("LED "); shell_puts(on ? "ON\n" : "OFF\n");
}

static void cmd_temp(int argc, char **argv) {
    (void)argc; (void)argv;
    // RP2040 temperature sensor
    // ADC channel 4
    
    // Simplified - would need proper ADC init
    shell_puts("Temperature: ~25C (stub)\n");
}

static void cmd_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    
    tick_t ticks = timer_ticks();
    u32 secs = ticks / PICOMIMI_TICK_HZ;
    u32 mins = secs / 60;
    u32 hours = mins / 60;
    
    shell_puts("Uptime: ");
    print_num(hours); shell_putc(':');
    if ((mins % 60) < 10) shell_putc('0');
    print_num(mins % 60); shell_putc(':');
    if ((secs % 60) < 10) shell_putc('0');
    print_num(secs % 60);
    shell_puts(" (");
    print_num(ticks);
    shell_puts(" ticks)\n");
}

static void cmd_free(int argc, char **argv) {
    cmd_mem(argc, argv);
}

static void cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_puts("Rebooting...\n");
    
    // RP2040 soft reset via watchdog
    *(volatile u32 *)0x40058000 = (1 << 31);  // WATCHDOG_CTRL = force
    
    while (1);
}

static void cmd_top(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_puts("Press any key to exit...\n\n");
    
    for (int i = 0; i < 5; i++) {
        shell_puts("\033[H\033[2J");  // Clear
        shell_puts("=== Picomimi Top ===\n\n");
        
        cmd_uptime(0, NULL);
        shell_puts("\n");
        cmd_ps(0, NULL);
        
        // Simple delay
        for (volatile int j = 0; j < 1000000; j++);
        
        // Check for keypress
        // Would need non-blocking getc
    }
}

// ============================================================================
// COMMAND TABLE
// ============================================================================

typedef struct {
    const char *name;
    void (*func)(int argc, char **argv);
} command_t;

static command_t commands[] = {
    {"help",    cmd_help},
    {"ps",      cmd_ps},
    {"mem",     cmd_mem},
    {"sched",   cmd_sched},
    {"domains", cmd_domains},
    {"top",     cmd_top},
    {"gpio",    cmd_gpio},
    {"led",     cmd_led},
    {"temp",    cmd_temp},
    {"uptime",  cmd_uptime},
    {"free",    cmd_free},
    {"reboot",  cmd_reboot},
    {NULL, NULL}
};

// ============================================================================
// COMMAND EXECUTION
// ============================================================================

static void shell_execute(void) {
    if (line_pos == 0) return;
    
    // Add to history
    if (history_count < SHELL_HISTORY_SIZE) {
        strcpy(history[history_count++], line_buffer);
    }
    
    // Parse args
    char *args[SHELL_MAX_ARGS];
    int argc = 0;
    
    char *p = line_buffer;
    while (*p && argc < SHELL_MAX_ARGS) {
        while (*p == ' ') p++;
        if (!*p) break;
        args[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    
    if (argc == 0) return;
    
    // Find command
    for (int i = 0; commands[i].name; i++) {
        if (strcmp(args[0], commands[i].name) == 0) {
            commands[i].func(argc, args);
            return;
        }
    }
    
    shell_puts(args[0]);
    shell_puts(": command not found\n");
}

// ============================================================================
// SHELL MAIN LOOP
// ============================================================================

void shell_init(void) {
    shell_puts("\n");
    shell_puts("+------------------------------------------+\n");
    shell_puts("|   ____  _                      _         |\n");
    shell_puts("|  |  _ \\(_) ___ ___  _ __ ___ (_)        |\n");
    shell_puts("|  | |_) | |/ __/ _ \\| '_ ` _ \\| |        |\n");
    shell_puts("|  |  __/| | (_| (_) | | | | | | |        |\n");
    shell_puts("|  |_|   |_|\\___\\___/|_| |_| |_|_|        |\n");
    shell_puts("|                                          |\n");
    shell_puts("|  Picomimi v" PICOMIMI_VERSION " - ARM Cortex-M RTOS     |\n");
    shell_puts("|  Type 'help' for commands                |\n");
    shell_puts("+------------------------------------------+\n");
    shell_puts("\n");
}

void shell_run(void) {
    shell_init();
    
    while (1) {
        shell_puts("picomimi> ");
        line_pos = 0;
        line_buffer[0] = '\0';
        
        while (1) {
            int c = shell_getc();
            
            if (c == '\r' || c == '\n') {
                shell_puts("\n");
                line_buffer[line_pos] = '\0';
                shell_execute();
                break;
            }
            
            if (c == '\b' || c == 127) {
                if (line_pos > 0) {
                    line_pos--;
                    shell_puts("\b \b");
                }
                continue;
            }
            
            if (c == 0x03) {  // Ctrl+C
                shell_puts("^C\n");
                break;
            }
            
            if (c >= 32 && c < 127 && line_pos < SHELL_MAX_LINE - 1) {
                line_buffer[line_pos++] = c;
                shell_putc(c);
            }
        }
    }
}

// ============================================================================
// SHELL TASK
// ============================================================================

void shell_task(void *arg) {
    (void)arg;
    shell_run();
}
