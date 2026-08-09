#ifndef FORGIX_APPLICATION_DIAGNOSTICS_H
#define FORGIX_APPLICATION_DIAGNOSTICS_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include <stdint.h>

#include "application_diagnostics_markers.h"
#include "bsp.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


enum
{
    APPLICATION_DIAGNOSTICS_WATCHDOG_TIMEOUT_MS = 5000,
    APPLICATION_DIAGNOSTICS_LED_HALF_PERIOD_MS = 250,
    APPLICATION_DIAGNOSTICS_SAMPLE_PERIOD_MS = 1000,
    /* Start-of-frame advances every millisecond while the host is framing, so a
       few seconds of silence is already decisive. */
    APPLICATION_DIAGNOSTICS_FRAME_STALL_MS = 5000,
    /* How many consecutive failing 1 Hz samples the FPGA check needs before it
       revokes readiness and, when recovery is enabled, attempts to
       reconfigure. Bench evidence (2026-08): the bit-banged bus misreads
       roughly once per boot session -- a single-sample transient that never
       repeats -- while a real fault fails every sample. Three in a row
       separates the two at a cost of two extra seconds of detection latency;
       every failing sample is still counted and attributed regardless. */
    APPLICATION_DIAGNOSTICS_FPGA_FAULT_SAMPLES = 3,
    /* How long the transmit FIFO must be observed continuously full with no
       outbound transfer completing before the heartbeat calls it wedged.
       Measured from the first sample that saw the FIFO full with the TX
       counter unmoved: quiet history never counts against a FIFO that only
       just filled, and only TX progress -- not inbound traffic -- can clear
       the run, because RX proves nothing about the transmit path. */
    APPLICATION_DIAGNOSTICS_FIFO_STALL_MS = 30000,
};




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


/* Reports the previous boot and arms the watchdog. Must run before the
   foreground loop starts, and before anything overwrites the retained scratch. */
void application_diagnostics_start( void );

/* First call of every foreground iteration: feeds the watchdog, drives the
   heartbeat LED, and once per second samples USB and FPGA health. */
void application_diagnostics_poll( void );

/* Live counters plus the retained report from the previous boot. */
void application_diagnostics_print_report( void );

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
void application_diagnostics_release_led( void );

void application_diagnostics_reclaim_led( void );

/* The boot cause as it was latched by application_diagnostics_start, before the
   watchdog was armed. Anything asking later must come here rather than call
   BSP_WatchdogBootReason again: arming the watchdog writes the scratch word that
   watchdog_enable_caused_reboot consults, so a live query minutes into a session
   reports a watchdog reset on a board that powered up cleanly. */
bsp_boot_reason application_diagnostics_boot_reason( void );

#ifdef __cplusplus
}
#endif

#endif
