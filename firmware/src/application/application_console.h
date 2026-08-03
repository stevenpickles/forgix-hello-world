#ifndef FORGIX_APPLICATION_CONSOLE_H
#define FORGIX_APPLICATION_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

enum {
    APPLICATION_IDLE_TIMEOUT_MS = 10000,
    APPLICATION_IDLE_STATUS_PERIOD_MS = 10000,
    APPLICATION_WATCH_MIN_SECONDS = 1,
    APPLICATION_WATCH_MAX_SECONDS = 3600,
};

/* The console no longer reads its own characters. The UI layer owns the terminal
   and decides whether a byte belongs to the shell, so it hands one in through
   application_console_feed and calls application_console_idle on the polls where
   nothing arrived. Splitting it this way keeps exactly one reader, which matters
   because that read is the only thing in the foreground loop that yields. */
void application_console_start(void);
void application_console_feed(int16_t character);
void application_console_idle(void);
void application_console_set_echo(bool enabled);
void application_console_set_quiet(bool enabled);
void application_console_set_watch(uint32_t period_seconds);
void application_console_disable_watch(void);

#endif
