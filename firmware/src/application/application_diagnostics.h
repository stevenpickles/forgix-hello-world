#ifndef FORGIX_APPLICATION_DIAGNOSTICS_H
#define FORGIX_APPLICATION_DIAGNOSTICS_H

#include <stdint.h>

/* Progress markers written to the watchdog marker register. After a watchdog
   reset the retained value names the code path that stopped making progress. */
enum {
    APPLICATION_DIAGNOSTICS_MARKER_LOOP = 1,
    APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_READ = 2,
    APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE = 3,
    APPLICATION_DIAGNOSTICS_MARKER_COMMAND = 4,
    APPLICATION_DIAGNOSTICS_MARKER_USB_SNAPSHOT = 5,
    APPLICATION_DIAGNOSTICS_MARKER_FPGA_CHECK = 6,
};

enum {
    APPLICATION_DIAGNOSTICS_WATCHDOG_TIMEOUT_MS = 5000,
    APPLICATION_DIAGNOSTICS_LED_HALF_PERIOD_MS = 250,
    APPLICATION_DIAGNOSTICS_SAMPLE_PERIOD_MS = 1000,
    APPLICATION_DIAGNOSTICS_STALL_TIMEOUT_MS = 5000,
};

/* Reports the previous boot and arms the watchdog. Must run before the
   foreground loop starts, and before anything overwrites the retained scratch. */
void application_diagnostics_start(void);

/* First call of every foreground iteration: feeds the watchdog, drives the
   heartbeat LED, and once per second samples USB and FPGA health. */
void application_diagnostics_poll(void);

/* Live counters plus the retained report from the previous boot. */
void application_diagnostics_print_report(void);

#endif
