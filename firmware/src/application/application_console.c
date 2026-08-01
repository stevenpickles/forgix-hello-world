#include "application.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "application_console.h"
#include "bsp.h"

enum { COMMAND_CAPACITY = 128 };

typedef enum {
    STATUS_DISABLED,
    STATUS_BOOT,
    STATUS_IDLE,
    STATUS_WATCH,
} status_mode_t;

typedef struct {
    char line[COMMAND_CAPACITY];
    size_t used;
    bool echo_enabled;
    bool quiet;
    bool auto_status_enabled;
    bool swallow_lf;
    status_mode_t status_mode;
    uint32_t current_time_ms;
    uint32_t next_status_ms;
    uint32_t status_period_ms;
} console_state_t;

static console_state_t console;

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void print_prompt(void) {
    if (!console.quiet) {
        bsp_console_printf("forgix> ");
    }
}

static void schedule_idle_status(void) {
    if (console.quiet || !console.auto_status_enabled) {
        console.status_mode = STATUS_DISABLED;
        return;
    }

    console.status_mode = STATUS_IDLE;
    console.status_period_ms = APPLICATION_IDLE_STATUS_PERIOD_MS;
    console.next_status_ms = console.current_time_ms + APPLICATION_IDLE_TIMEOUT_MS;
}

static void stop_active_status(void) {
    console.status_mode = STATUS_DISABLED;
}

static void echo_character(int character) {
    if (!console.quiet && console.echo_enabled) {
        bsp_console_putchar(character);
    }
}

static void erase_character(void) {
    if (!console.quiet && console.echo_enabled) {
        bsp_console_printf("\b \b");
    }
}

static void complete_line(void) {
    if (!console.quiet && console.echo_enabled) {
        bsp_console_printf("\r\n");
    }

    if (console.used) {
        console.line[console.used] = 0;
        application_process_command(console.line);
        console.used = 0;
    }

    if (console.status_mode != STATUS_WATCH) {
        schedule_idle_status();
    }
    print_prompt();
}

static void cancel_line(void) {
    console.used = 0;
    if (!console.quiet) {
        bsp_console_printf("^C\r\n");
    }
    schedule_idle_status();
    print_prompt();
}

static void redraw_line(void) {
    if (!console.quiet) {
        bsp_console_printf("\r\nforgix> %.*s", (int)console.used, console.line);
    }
}

static void process_character(int character) {
    if (character == '\n' && console.swallow_lf) {
        console.swallow_lf = false;
        return;
    }
    console.swallow_lf = false;
    stop_active_status();

    if (character == '\r' || character == '\n') {
        console.swallow_lf = character == '\r';
        complete_line();
    } else if (character == 3) {
        cancel_line();
    } else if (character == 12) {
        redraw_line();
    } else if (character == 21) {
        while (console.used) {
            --console.used;
            erase_character();
        }
    } else if (character == '\b' || character == 127) {
        if (console.used) {
            --console.used;
            erase_character();
        } else {
            echo_character('\a');
        }
    } else if (isprint((unsigned char)character)) {
        if (console.used + 1 < sizeof console.line) {
            console.line[console.used++] = (char)character;
            echo_character(character);
        } else {
            echo_character('\a');
        }
    }
}

void application_console_start(void) {
    console = (console_state_t){
        .echo_enabled = true,
        .auto_status_enabled = true,
        .status_mode = STATUS_BOOT,
        .status_period_ms = APPLICATION_BOOT_STATUS_PERIOD_MS,
    };
    console.current_time_ms = bsp_time_now_ms();
    console.next_status_ms = console.current_time_ms + console.status_period_ms;
    print_prompt();
}

void application_console_poll(void) {
    int character = bsp_console_getchar_timeout_us(1000);
    console.current_time_ms = bsp_time_now_ms();

    if (character != BSP_CONSOLE_TIMEOUT) {
        process_character(character);
        return;
    }
    if (console.quiet || console.used || console.status_mode == STATUS_DISABLED ||
            !deadline_reached(console.current_time_ms, console.next_status_ms)) {
        return;
    }

    bsp_console_printf("\r\n");
    application_print_status();
    print_prompt();
    console.next_status_ms = console.current_time_ms + console.status_period_ms;
}

void application_console_set_echo(bool enabled) {
    console.echo_enabled = enabled;
}

void application_console_set_quiet(bool enabled) {
    console.quiet = enabled;
    console.echo_enabled = !enabled;
    console.auto_status_enabled = !enabled;
    stop_active_status();
}

void application_console_set_watch(uint32_t period_seconds) {
    console.quiet = false;
    console.auto_status_enabled = true;
    console.status_mode = STATUS_WATCH;
    console.status_period_ms = period_seconds * 1000u;
    console.next_status_ms = console.current_time_ms + console.status_period_ms;
}

void application_console_disable_watch(void) {
    console.auto_status_enabled = false;
    stop_active_status();
}
