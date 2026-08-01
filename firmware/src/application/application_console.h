#ifndef FORGIX_APPLICATION_CONSOLE_H
#define FORGIX_APPLICATION_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

enum {
    APPLICATION_BOOT_STATUS_PERIOD_MS = 1000,
    APPLICATION_IDLE_TIMEOUT_MS = 10000,
    APPLICATION_IDLE_STATUS_PERIOD_MS = 10000,
    APPLICATION_WATCH_MIN_SECONDS = 1,
    APPLICATION_WATCH_MAX_SECONDS = 3600,
};

void application_console_start(void);
void application_console_poll(void);
void application_console_set_echo(bool enabled);
void application_console_set_quiet(bool enabled);
void application_console_set_watch(uint32_t period_seconds);
void application_console_disable_watch(void);

#endif
