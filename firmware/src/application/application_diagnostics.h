#ifndef FORGIX_APPLICATION_DIAGNOSTICS_H
#define FORGIX_APPLICATION_DIAGNOSTICS_H

#include <stdint.h>

#include "bsp.h"

/* Progress markers written to the watchdog marker register. After a watchdog
   reset the retained value names the code path that stopped making progress. */
enum {
    APPLICATION_DIAGNOSTICS_MARKER_LOOP = 1,
    APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_READ = 2,
    APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE = 3,
    APPLICATION_DIAGNOSTICS_MARKER_COMMAND = 4,
    APPLICATION_DIAGNOSTICS_MARKER_USB_SNAPSHOT = 5,
    APPLICATION_DIAGNOSTICS_MARKER_FPGA_CHECK = 6,
    /* The menu and the built-in test are the newest code in the foreground loop
       and so the likeliest to stall it. Without their own markers a watchdog
       reset from either would be attributed to whatever ran last instead. */
    APPLICATION_DIAGNOSTICS_MARKER_MENU = 7,
    APPLICATION_DIAGNOSTICS_MARKER_IBIT = 8,
    APPLICATION_DIAGNOSTICS_MARKER_EFFECT = 9,
};

enum {
    APPLICATION_DIAGNOSTICS_WATCHDOG_TIMEOUT_MS = 5000,
    APPLICATION_DIAGNOSTICS_LED_HALF_PERIOD_MS = 250,
    APPLICATION_DIAGNOSTICS_SAMPLE_PERIOD_MS = 1000,
    /* Start-of-frame advances every millisecond while the host is framing, so a
       few seconds of silence is already decisive. */
    APPLICATION_DIAGNOSTICS_FRAME_STALL_MS = 5000,
    /* CDC traffic is bursty and driven by whatever the host and the console
       policy do, so a gap proves nothing on its own. This must stay well above
       APPLICATION_IDLE_STATUS_PERIOD_MS, and is only consulted alongside a
       backed-up transmit FIFO. */
    APPLICATION_DIAGNOSTICS_ACTIVITY_STALL_MS = 30000,
};

/* Reports the previous boot and arms the watchdog. Must run before the
   foreground loop starts, and before anything overwrites the retained scratch. */
void application_diagnostics_start(void);

/* First call of every foreground iteration: feeds the watchdog, drives the
   heartbeat LED, and once per second samples USB and FPGA health. */
void application_diagnostics_poll(void);

/* Live counters plus the retained report from the previous boot. */
void application_diagnostics_print_report(void);

/* Hands the LED to something else for as long as it needs it, and takes it back.
   The heartbeat rewrites the LED every 250 ms, which is faster than anything a
   person can watch: a light show or an LED test that holds a colour for longer
   than that gets the heartbeat punched through the middle of it.

   Both calls are needed, not just the first. The FPGA health check reads the LED
   back and compares it against what the heartbeat last commanded, so a heartbeat
   that merely stopped writing would leave that comparison judging a command it
   no longer issues and counting an FPGA failure every second. Releasing stands
   both of them down together; reclaiming restores the heartbeat and refreshes
   what the check compares against, in that order. */
void application_diagnostics_release_led(void);

void application_diagnostics_reclaim_led(void);

/* The boot cause as it was latched by application_diagnostics_start, before the
   watchdog was armed. Anything asking later must come here rather than call
   BSP_WatchdogBootReason again: arming the watchdog writes the scratch word that
   watchdog_enable_caused_reboot consults, so a live query minutes into a session
   reports a watchdog reset on a board that powered up cleanly. */
bsp_boot_reason application_diagnostics_boot_reason(void);

#endif
