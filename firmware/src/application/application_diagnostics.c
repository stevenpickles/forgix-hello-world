/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_diagnostics.h"

#include <stdbool.h>
#include <stdint.h>

#include "application_diagnostics_internal.h"
#include "application_diagnostics_led.h"
#include "application_diagnostics_report.h"
#include "application_time.h"
#include "bsp.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


enum
{
    /* Six heartbeat toggles mark a successful FPGA reconfiguration -- four
       white on-phases in all, because the repaint at the reconfiguring sample
       lands an extra one before the six countdown toggles begin. A frozen LED
       that resumes with this signature proves the MCU stayed alive and the
       FPGA lost its configuration. */
    RECOVERY_TOGGLES = 6
};


/* Packing of snapshot slot 2. Slot 0 holds the loop-seconds counter and slot 1
   the raw USB activity counter. The FPGA counters are narrow modulo fields; the
   full-width values stay available live through the `diag` command. */
enum
{
    HEALTH_FRAME_MASK = 0xffffu,
    HEALTH_CONNECTED_SHIFT = 16,
    HEALTH_SUSPENDED_SHIFT = 17,
    HEALTH_WRITE_BLOCKED_SHIFT = 18,
    HEALTH_FPGA_FAILURE_SHIFT = 19,
    HEALTH_FPGA_FAILURE_MASK = 0x7fu,
    HEALTH_FPGA_RECONFIGURE_SHIFT = 26,
    HEALTH_FPGA_RECONFIGURE_MASK = 0x3fu,
};




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* Not static: the LED policy and the reporting live in their own files and both
   read this record through the extern in application_diagnostics_internal.h.
   The three files are one module split by concern -- what the board has been
   doing is a single fact, and a copy of it would let two of them disagree. */
diagnostics_state_t application_diagnostics_state;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static void check_fpga( uint32_t now_ms );

static void sample_usb( uint32_t now_ms );

static uint32_t packed_health( void );

static void store_snapshots( void );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Copies the retained scratch registers out and zeroes the snapshot slots
///     in the same pass, so the loop below cannot mix this run's samples into
///     the previous boot's evidence. The BSP's early watchdog is already live;
///     the blink reporter feeds it, then the final Start narrows its window for
///     the foreground loop.
/// </summary>
void application_diagnostics_start( void )
{
    application_diagnostics_state = ( diagnostics_state_t ){ 0 };
    application_diagnostics_state.usb_present = BSP_UsbPresent();
    application_diagnostics_state.boot_reason = BSP_WatchdogBootReason();
    application_diagnostics_state.boot_marker = BSP_WatchdogBootMarker();
    for ( uint32_t slot = 0; slot < BSP_WATCHDOG_SNAPSHOT_SLOTS; ++slot )
    {
        application_diagnostics_state.boot_snapshot[ slot ] = BSP_WatchdogSnapshotGet( slot );
        BSP_WatchdogSnapshotSet( slot, 0 );
    }

    /* Printing is unconditional: with no stdio backend linked it costs nothing,
       and when the USB-free image is built with FORGIX_DIAGNOSTIC_UART it is the
       only report that survives the FPGA dying. The blink code is additional,
       for the console-less build. */
    application_diagnostics_report_boot();
    if ( !application_diagnostics_state.usb_present )
    {
        application_diagnostics_report_blink();
    }

    uint32_t now_ms = BSP_TimeNowMs();
    application_diagnostics_state.led_on = true;
    application_diagnostics_state.next_led_ms = now_ms + APPLICATION_DIAGNOSTICS_LED_HALF_PERIOD_MS;
    application_diagnostics_state.next_sample_ms =
        now_ms + APPLICATION_DIAGNOSTICS_SAMPLE_PERIOD_MS;
    /* The stall run needs no seed: the wholesale zeroing above cleared the
       flag, and the epoch is only ever read while the flag is set. */
    application_diagnostics_state.last_frame_ms = now_ms;
    application_diagnostics_apply_led( now_ms );

    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_LOOP );
    BSP_WatchdogStart( APPLICATION_DIAGNOSTICS_WATCHDOG_TIMEOUT_MS );
}

/// <summary>
///     Feeds the watchdog and re-stamps the loop marker on every pass; the rest
///     waits on its own deadline, so the ordinary pass is two register writes.
///     While the LED is released nothing is written to the FPGA, but the blink
///     phase and the recovery countdown keep advancing underneath.
/// </summary>
void application_diagnostics_poll( void )
{
    BSP_WatchdogFeed();
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_LOOP );

    uint32_t now_ms = BSP_TimeNowMs();
    bool led_due =
        application_deadline_reached( now_ms, application_diagnostics_state.next_led_ms );
    bool sample_due =
        application_deadline_reached( now_ms, application_diagnostics_state.next_sample_ms );

    /* Sampling first means the heartbeat color below reflects the health just
       read, and the single LED write is the one the FPGA check reads back. */
    if ( sample_due )
    {
        application_diagnostics_state.next_sample_ms =
            now_ms + APPLICATION_DIAGNOSTICS_SAMPLE_PERIOD_MS;
        ++application_diagnostics_state.uptime_seconds;
        sample_usb( now_ms );
    }
    if ( led_due )
    {
        application_diagnostics_state.next_led_ms =
            now_ms + APPLICATION_DIAGNOSTICS_LED_HALF_PERIOD_MS;
        application_diagnostics_state.led_on = !application_diagnostics_state.led_on;
    }
    if ( ( led_due || sample_due ) && !application_diagnostics_state.led_released )
    {
        application_diagnostics_apply_led( now_ms );
    }
    if ( led_due && application_diagnostics_state.recovery_toggles )
    {
        --application_diagnostics_state.recovery_toggles;
    }
    if ( sample_due )
    {
        check_fpga( now_ms );
        store_snapshots();
        /* One line per second in the USB-free image. On a UART build this is the
           MCU-liveness proof the LED cannot give: it depends on nothing but the
           foreground loop, so the last logged second dates the freeze exactly.
           The shell image omits it, where it would flood the console. */
        if ( !application_diagnostics_state.usb_present )
        {
            application_diagnostics_report_live();
        }
        BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_LOOP );
    }
}

/// <summary>
///     Hands back the value latched once at start-up, which never changes for the
///     life of the run. Before start has run it reads BSP_BOOT_POWER_ON, because
///     that is what a zeroed state block spells -- an answer indistinguishable
///     from a real clean boot, so nothing may consult this before start.
/// </summary>
/// <returns>
///     The boot cause as it was at start, not as the hardware reports it now.
/// </returns>
bsp_boot_reason application_diagnostics_boot_reason( void )
{
    return application_diagnostics_state.boot_reason;
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/* Runs immediately after the heartbeat LED write, so the readback measures the
   FPGA bus rather than whatever a `color` command left behind between polls. */
/// <summary>
///     Charges at most one failure per sample however many of the three checks
///     went wrong, attributed to the first term that failed -- the pin before
///     the ping, because pinging an unconfigured part proves nothing, and the
///     ping before the readback, because the readback needs a design to answer.
///     Acting on the fault is debounced behind consecutive failing samples; the
///     counting is not. Leaves its own marker standing on return, so a hang
///     inside the bus access is attributed here and not to the caller.
/// </summary>
static void check_fpga( uint32_t now_ms )
{
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_FPGA_CHECK );

    /* The readback is only evidence while the heartbeat is the thing driving the
       LED. Once it has been handed over, what is in those registers belongs to
       whoever holds it, and comparing it against a stale command would report an
       FPGA fault once a second for the length of every light show. CDONE and the
       design-ID ping still answer for the FPGA. */
    if ( !BSP_FpgaCdone() )
    {
        ++application_diagnostics_state.fpga_cdone_failures;
    }
    else if ( BSP_FpgaPing() != BSP_FPGA_DESIGN_ID )
    {
        ++application_diagnostics_state.fpga_ping_failures;
    }
    else if ( !application_diagnostics_state.led_released &&
              !application_diagnostics_led_readback_matches() )
    {
        ++application_diagnostics_state.fpga_readback_failures;
    }
    else
    {
        application_diagnostics_state.fpga_consecutive_failures = 0;
        return;
    }

    ++application_diagnostics_state.fpga_failures;
    ++application_diagnostics_state.fpga_consecutive_failures;

    /* The debounce. Bench evidence: this bus misreads about one sample per
       boot session, and a misread does not repeat, while a real fault fails
       every sample. Revoking on the first failure turned each transient into a
       shell gated until reboot; three in a row costs two seconds of latency on
       a genuine fault and nothing on a misread. */
    if ( application_diagnostics_state.fpga_consecutive_failures <
         APPLICATION_DIAGNOSTICS_FPGA_FAULT_SAMPLES )
    {
        return;
    }

    /* This is what pulls the menu line and the command gate down when the FPGA
       dies at runtime. Without it the readiness latch keeps its boot-time value
       and the shell would go on offering commands to a part that stopped
       answering. A later passing sample does not set it back -- only a
       successful reconfiguration rewrites the latch, through the same bring-up
       that set it at boot. */
    BSP_FpgaMarkUnresponsive();

    /* Recovery is opt-in. Reloading the bitstream drives CRESET_N and rewrites
       173 KB on every failing sample past the debounce, which is itself a
       disturbance; keeping it off establishes what the fault does when left
       alone. */
    if ( !BSP_FpgaAutoReconfigureEnabled() )
    {
        return;
    }
    if ( BSP_FpgaReconfigure() )
    {
        ++application_diagnostics_state.fpga_reconfigures;
        application_diagnostics_state.recovery_toggles = RECOVERY_TOGGLES;
        /* The fresh configuration starts a fresh verdict: its registers are
           cleared, so the commanded heartbeat state has to be written again --
           but only while the heartbeat owns the LED. Released, those registers
           are the new owner's to fill, and reclaim repaints unconditionally
           when the handover ends. */
        application_diagnostics_state.fpga_consecutive_failures = 0;
        if ( !application_diagnostics_state.led_released )
        {
            application_diagnostics_apply_led( now_ms );
        }
    }
}

/// <summary>
///     Tracks the transmit-stall run and the frame clock. The stall epoch is
///     the first sample that saw the FIFO full with the TX completion counter
///     unmoved, and the run breaks on observed room or on TX progress -- TX
///     only, on purpose: inbound traffic proves nothing about a wedged
///     transmit endpoint, and the pooled activity counter used to let RX
///     conceal exactly that fault. A TX completion also clears a run whose
///     FIFO drained and refilled between samples, since it proves the queued
///     data moved. Nothing here judges health; it only records the run.
/// </summary>
static void sample_usb( uint32_t now_ms )
{
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_USB_SNAPSHOT );
    application_diagnostics_state.health = BSP_UsbHealth();

    /* != rather than an ordered compare, so the counter wrapping past zero
       still reads as progress. */
    const bool tx_moved = application_diagnostics_state.health.tx_complete_count !=
                          application_diagnostics_state.last_tx_count;
    if ( tx_moved )
    {
        application_diagnostics_state.last_tx_count =
            application_diagnostics_state.health.tx_complete_count;
    }
    if ( application_diagnostics_state.health.write_available > 0u || tx_moved )
    {
        application_diagnostics_state.fifo_stalled = false;
    }
    else if ( !application_diagnostics_state.fifo_stalled )
    {
        application_diagnostics_state.fifo_stalled = true;
        application_diagnostics_state.fifo_stall_epoch_ms = now_ms;
    }
    if ( application_diagnostics_state.health.frame_number !=
         application_diagnostics_state.last_frame_number )
    {
        application_diagnostics_state.last_frame_number =
            application_diagnostics_state.health.frame_number;
        application_diagnostics_state.last_frame_ms = now_ms;
    }
}

/// <summary>
///     Squeezes six values into one retained word, and the counters wrap inside
///     their masks rather than saturating: a failure field reading zero means
///     none, or an exact multiple of 128. Only the flags are safe to read
///     literally, which is why the full-width counters stay on the live report.
/// </summary>
/// <returns>
///     The frame number, health flags and fault counters packed for slot 2.
/// </returns>
static uint32_t packed_health( void )
{
    uint32_t packed = application_diagnostics_state.health.frame_number & HEALTH_FRAME_MASK;
    packed |= (uint32_t) application_diagnostics_state.health.connected << HEALTH_CONNECTED_SHIFT;
    packed |= (uint32_t) application_diagnostics_state.health.suspended << HEALTH_SUSPENDED_SHIFT;
    packed |= (uint32_t) ( application_diagnostics_state.health.write_available == 0 )
              << HEALTH_WRITE_BLOCKED_SHIFT;
    packed |= ( application_diagnostics_state.fpga_failures & HEALTH_FPGA_FAILURE_MASK )
              << HEALTH_FPGA_FAILURE_SHIFT;
    packed |= ( application_diagnostics_state.fpga_reconfigures & HEALTH_FPGA_RECONFIGURE_MASK )
              << HEALTH_FPGA_RECONFIGURE_SHIFT;
    return packed;
}

/// <summary>
///     Rewrites all three slots every sample, so a set recovered after a reset
///     never mixes fields captured a second apart. It also overwrites what the
///     previous boot left behind, which is why start copies those values into RAM
///     before the loop is allowed to run.
/// </summary>
static void store_snapshots( void )
{
    BSP_WatchdogSnapshotSet( 0, application_diagnostics_state.uptime_seconds );
    BSP_WatchdogSnapshotSet( 1, application_diagnostics_state.health.activity_count );
    BSP_WatchdogSnapshotSet( 2, packed_health() );
}
