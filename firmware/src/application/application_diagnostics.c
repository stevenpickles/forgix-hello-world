/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_diagnostics.h"

#include <stdbool.h>
#include <stdint.h>

#include "bsp.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


enum
{
    HEARTBEAT_BRIGHTNESS = 64,
    /* Six heartbeat toggles mark a successful FPGA reconfiguration -- four
       white on-phases in all, because the repaint at the reconfiguring sample
       lands an extra one before the six countdown toggles begin. A frozen LED
       that resumes with this signature proves the MCU stayed alive and the
       FPGA lost its configuration. */
    RECOVERY_TOGGLES = 6,
    BOOT_BLINK_MAX = 8,
    /* The blink code is the USB-free image's only boot-evidence channel, and it
       plays once: a power cycle to see it again would destroy the very scratch
       registers it is reporting. So it is paced to be readable and repeated. */
    BOOT_BLINK_ON_MS = 350,
    BOOT_BLINK_OFF_MS = 250,
    BOOT_BLINK_GAP_MS = 800,
    BOOT_REPORT_REPEATS = 3,
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


typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint32_t blinks;
} boot_signature_t;


typedef struct
{
    bool usb_present;
    bsp_boot_reason boot_reason;
    uint32_t boot_marker;
    uint32_t boot_snapshot[ BSP_WATCHDOG_SNAPSHOT_SLOTS ];

    bool led_on;
    /* True while something else owns the LED. The phase keeps advancing
       underneath, so the heartbeat picks up where it would have been rather than
       restarting whenever an activity ends. */
    bool led_released;
    uint32_t next_led_ms;
    uint32_t next_sample_ms;
    uint32_t uptime_seconds;
    uint32_t recovery_toggles;
    bsp_led_state_t commanded;

    bsp_usb_health_t health;
    uint32_t last_activity_count;
    uint32_t last_activity_ms;
    uint32_t last_frame_number;
    uint32_t last_frame_ms;

    uint32_t fpga_failures;
    uint32_t fpga_reconfigures;
} diagnostics_state_t;




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


static diagnostics_state_t diagnostics;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bool deadline_reached( uint32_t now_ms, uint32_t deadline_ms );

static bool stalled_since( uint32_t now_ms, uint32_t since_ms, uint32_t threshold_ms );

static void resting_color( uint8_t *red, uint8_t *green, uint8_t *blue );

static void heartbeat_color( uint32_t now_ms, uint8_t *red, uint8_t *green, uint8_t *blue );

static void apply_led( uint32_t now_ms );

static bool led_readback_matches( void );

static void check_fpga( uint32_t now_ms );

static void sample_usb( uint32_t now_ms );

static uint32_t packed_health( void );

static void store_snapshots( void );

static const char *boot_reason_name( void );

static void print_boot_report( void );

static void print_live_report( void );

static uint32_t clamp_blinks( uint32_t marker );

static boot_signature_t boot_signature( void );

static void blink_boot_report( void );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Copies the retained scratch registers out and zeroes the snapshot slots
///     in the same pass, so the loop below cannot mix this run's samples into
///     the previous boot's evidence. The watchdog is armed last on purpose:
///     everything above that line, the blink code included, is free to block.
/// </summary>
void application_diagnostics_start( void )
{
    diagnostics = ( diagnostics_state_t ){ 0 };
    diagnostics.usb_present = BSP_UsbPresent();
    diagnostics.boot_reason = BSP_WatchdogBootReason();
    diagnostics.boot_marker = BSP_WatchdogMarkerGet();
    for ( uint32_t slot = 0; slot < BSP_WATCHDOG_SNAPSHOT_SLOTS; ++slot )
    {
        diagnostics.boot_snapshot[ slot ] = BSP_WatchdogSnapshotGet( slot );
        BSP_WatchdogSnapshotSet( slot, 0 );
    }

    /* Printing is unconditional: with no stdio backend linked it costs nothing,
       and when the USB-free image is built with FORGIX_DIAGNOSTIC_UART it is the
       only report that survives the FPGA dying. The blink code is additional,
       for the console-less build. */
    print_boot_report();
    if ( !diagnostics.usb_present )
    {
        blink_boot_report();
    }

    uint32_t now_ms = BSP_TimeNowMs();
    diagnostics.led_on = true;
    diagnostics.next_led_ms = now_ms + APPLICATION_DIAGNOSTICS_LED_HALF_PERIOD_MS;
    diagnostics.next_sample_ms = now_ms + APPLICATION_DIAGNOSTICS_SAMPLE_PERIOD_MS;
    diagnostics.last_activity_ms = now_ms;
    diagnostics.last_frame_ms = now_ms;
    apply_led( now_ms );

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
    bool led_due = deadline_reached( now_ms, diagnostics.next_led_ms );
    bool sample_due = deadline_reached( now_ms, diagnostics.next_sample_ms );

    /* Sampling first means the heartbeat color below reflects the health just
       read, and the single LED write is the one the FPGA check reads back. */
    if ( sample_due )
    {
        diagnostics.next_sample_ms = now_ms + APPLICATION_DIAGNOSTICS_SAMPLE_PERIOD_MS;
        ++diagnostics.uptime_seconds;
        sample_usb( now_ms );
    }
    if ( led_due )
    {
        diagnostics.next_led_ms = now_ms + APPLICATION_DIAGNOSTICS_LED_HALF_PERIOD_MS;
        diagnostics.led_on = !diagnostics.led_on;
    }
    if ( ( led_due || sample_due ) && !diagnostics.led_released )
    {
        apply_led( now_ms );
    }
    if ( led_due && diagnostics.recovery_toggles )
    {
        --diagnostics.recovery_toggles;
    }
    if ( sample_due )
    {
        check_fpga( now_ms );
        store_snapshots();
        /* One line per second in the USB-free image. On a UART build this is the
           MCU-liveness proof the LED cannot give: it depends on nothing but the
           foreground loop, so the last logged second dates the freeze exactly.
           The shell image omits it, where it would flood the console. */
        if ( !diagnostics.usb_present )
        {
            print_live_report();
        }
        BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_LOOP );
    }
}

/// <summary>
///     Replays the boot line from what start captured rather than re-reading the
///     hardware, which by now describes this run, then adds the live counters at
///     their full width -- the retained slots only carry them modulo the bit
///     fields they were packed into.
/// </summary>
void application_diagnostics_print_report( void )
{
    print_boot_report();
    BSP_ConsolePrintf(
        "diag: uptime=%lus connected=%u suspended=%u write=%lu activity=%lu sof=%lu "
        "fpga_fail=%lu fpga_reconfig=%lu\n",
        (unsigned long) diagnostics.uptime_seconds, diagnostics.health.connected,
        diagnostics.health.suspended, (unsigned long) diagnostics.health.write_available,
        (unsigned long) diagnostics.health.activity_count,
        (unsigned long) diagnostics.health.frame_number, (unsigned long) diagnostics.fpga_failures,
        (unsigned long) diagnostics.fpga_reconfigures );
}

/// <summary>
///     Stands down the heartbeat write and the readback comparison together, and
///     deliberately leaves the LED showing whatever it last commanded: the new
///     owner inherits a lit board rather than a dark one, and inherits it before
///     it has painted anything of its own.
/// </summary>
void application_diagnostics_release_led( void )
{
    diagnostics.led_released = true;
}

/// <summary>
///     Resumes the heartbeat at whatever phase it would have reached, not at the
///     start of a period, so a short light show does not visibly reset the blink.
///     Harmless without a matching release, which is what lets an aborted
///     activity's stop path call it unconditionally.
/// </summary>
void application_diagnostics_reclaim_led( void )
{
    diagnostics.led_released = false;
    /* Written immediately rather than at the next 250 ms edge. Waiting would
       leave whatever the last owner painted on the board for a quarter of a
       second after it stopped owning it, and -- worse -- would leave the FPGA
       health check comparing against a command that predates the handover if it
       samples first. */
    apply_led( BSP_TimeNowMs() );
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
    return diagnostics.boot_reason;
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/// <summary>
///     Subtracts and tests the sign rather than comparing the two values, so a
///     deadline that straddles the 32-bit millisecond wrap still fires instead of
///     parking the heartbeat for the next 49 days. A deadline exactly reached
///     counts as due.
/// </summary>
/// <returns>
///     True once now_ms has caught up with deadline_ms.
/// </returns>
static bool deadline_reached( uint32_t now_ms, uint32_t deadline_ms )
{
    return (int32_t) ( now_ms - deadline_ms ) >= 0;
}

/// <summary>
///     Elapsed-time test in the same wrap-safe signed form. The threshold is cast
///     to signed as well, so it has to stay well under 2^31 ms; the two stall
///     limits this serves are seconds, not days.
/// </summary>
/// <returns>
///     True once threshold_ms has passed since since_ms.
/// </returns>
static bool stalled_since( uint32_t now_ms, uint32_t since_ms, uint32_t threshold_ms )
{
    return (int32_t) ( now_ms - since_ms ) >= (int32_t) threshold_ms;
}

/* The USB-free image has no USB health to show, so its resting heartbeat carries
   the last boot reason instead. The blink code plays once and cannot be replayed
   without destroying the evidence, so this keeps the verdict readable for the
   whole run: blue is nominal, any other resting color means something happened. */
/// <summary>
///     Writes all three channels on every path, so the caller never has to clear
///     them first and no reason can leak a channel from the previous call. A
///     reason with no case of its own falls through to the blue a clean power-on
///     gets, so an unrecognised code reads as nominal rather than as a fault.
/// </summary>
static void resting_color( uint8_t *red, uint8_t *green, uint8_t *blue )
{
    *red = 0;
    *green = 0;
    *blue = 255; /* blue: clean power-on */

    switch ( diagnostics.boot_reason )
    {
    case BSP_BOOT_WATCHDOG:
        *red = 255;
        *blue = 0; /* red: the foreground stopped and the watchdog recovered it */
        break;
    case BSP_BOOT_BROWNOUT:
        *red = 255;
        *green = 255;
        *blue = 0; /* yellow: supply droop */
        break;
    case BSP_BOOT_OTHER:
        *green = 255; /* cyan: reset with no attributable cause */
        break;
    default:
        break;
    }
}

/// <summary>
///     The branch order is a severity ladder rather than a set of independent
///     tests: the recovery signature outranks any live verdict, and a link that
///     is both suspended and starved of transfers shows only the first colour
///     that matches. One LED cannot say two things at once.
/// </summary>
static void heartbeat_color( uint32_t now_ms, uint8_t *red, uint8_t *green, uint8_t *blue )
{
    if ( diagnostics.recovery_toggles )
    {
        *red = 255;
        *green = 255;
        *blue = 255; /* white: FPGA reconfiguration recovery signature */
    }
    else if ( !diagnostics.usb_present )
    {
        resting_color( red, green, blue );
    }
    else if ( !diagnostics.health.connected )
    {
        *red = 0;
        *green = 0;
        *blue = 255; /* blue: host has not asserted DTR */
    }
    else if ( diagnostics.health.suspended ||
              stalled_since( now_ms, diagnostics.last_frame_ms,
                             APPLICATION_DIAGNOSTICS_FRAME_STALL_MS ) )
    {
        *red = 255;
        *green = 0;
        *blue = 255; /* magenta: bus suspended or start-of-frame counter frozen */
    }
    else if ( diagnostics.health.write_available == 0 &&
              stalled_since( now_ms, diagnostics.last_activity_ms,
                             APPLICATION_DIAGNOSTICS_ACTIVITY_STALL_MS ) )
    {
        /* red: data is queued and the FIFO is not draining. A quiet link is not
           a fault -- keying on the gap alone made this trip on the firmware's
           own 10 s idle-status cadence, reporting a wedge every single cycle. */
        *red = 255;
        *green = 0;
        *blue = 0;
    }
    else
    {
        *red = 0;
        *green = 255;
        *blue = 0; /* green: connected and transfers are completing */
    }
}

/// <summary>
///     The only place the heartbeat touches the LED, and it records what it asked
///     for in the same step -- the FPGA readback check has nothing else to
///     compare against, so a write that bypassed this would be reported as a bus
///     fault. The dark half clears only the enable flag, matching BSP_LedOff,
///     which leaves the recorded colour still describing the registers.
/// </summary>
static void apply_led( uint32_t now_ms )
{
    if ( diagnostics.led_on )
    {
        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;
        heartbeat_color( now_ms, &red, &green, &blue );
        BSP_LedSet( red, green, blue, HEARTBEAT_BRIGHTNESS );
        diagnostics.commanded = ( bsp_led_state_t ){
            .red = red,
            .green = green,
            .blue = blue,
            .brightness = HEARTBEAT_BRIGHTNESS,
            .enabled = true,
        };
    }
    else
    {
        BSP_LedOff();
        diagnostics.commanded.enabled = false;
    }
}

/// <summary>
///     Compares every field, brightness and enable included, so a write that
///     latched only some of the registers fails here rather than passing on
///     colour alone. Meaningful only while the heartbeat still owns the LED, and
///     only immediately after apply_led has run in this pass.
/// </summary>
/// <returns>
///     True when the FPGA holds exactly what apply_led last commanded.
/// </returns>
static bool led_readback_matches( void )
{
    bsp_led_state_t led = BSP_LedGet();
    return led.red == diagnostics.commanded.red && led.green == diagnostics.commanded.green &&
           led.blue == diagnostics.commanded.blue &&
           led.brightness == diagnostics.commanded.brightness &&
           led.enabled == diagnostics.commanded.enabled;
}

/* Runs immediately after the heartbeat LED write, so the readback measures the
   FPGA bus rather than whatever a `color` command left behind between polls. */
/// <summary>
///     Charges at most one failure per sample however many of the three checks
///     went wrong, so fpga_failures counts seconds spent in fault rather than
///     tallying symptoms. Leaves its own marker standing on return, so a hang
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
    if ( BSP_FpgaCdone() && BSP_FpgaPing() == BSP_FPGA_DESIGN_ID &&
         ( diagnostics.led_released || led_readback_matches() ) )
    {
        return;
    }

    ++diagnostics.fpga_failures;

    /* Recovery is opt-in. Reloading the bitstream drives CRESET_N and rewrites
       173 KB on every failing sample, which is itself a disturbance; keeping it
       off establishes what the fault does when left alone. */
    if ( !BSP_FpgaAutoReconfigureEnabled() )
    {
        return;
    }
    if ( BSP_FpgaReconfigure() )
    {
        ++diagnostics.fpga_reconfigures;
        diagnostics.recovery_toggles = RECOVERY_TOGGLES;
        /* The fresh configuration comes up with its registers cleared, so the
           commanded heartbeat state has to be written again -- but only while
           the heartbeat owns the LED. Released, those registers are the new
           owner's to fill, and reclaim repaints unconditionally when the
           handover ends. */
        if ( !diagnostics.led_released )
        {
            apply_led( now_ms );
        }
    }
}

/// <summary>
///     The two timestamps move only when their counter actually changed, which is
///     what makes the stall thresholds measure a counter standing still rather
///     than the time since the last sample. Nothing here judges health; it only
///     records when progress was last seen.
/// </summary>
static void sample_usb( uint32_t now_ms )
{
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_USB_SNAPSHOT );
    diagnostics.health = BSP_UsbHealth();

    if ( diagnostics.health.activity_count != diagnostics.last_activity_count )
    {
        diagnostics.last_activity_count = diagnostics.health.activity_count;
        diagnostics.last_activity_ms = now_ms;
    }
    if ( diagnostics.health.frame_number != diagnostics.last_frame_number )
    {
        diagnostics.last_frame_number = diagnostics.health.frame_number;
        diagnostics.last_frame_ms = now_ms;
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
    uint32_t packed = diagnostics.health.frame_number & HEALTH_FRAME_MASK;
    packed |= (uint32_t) diagnostics.health.connected << HEALTH_CONNECTED_SHIFT;
    packed |= (uint32_t) diagnostics.health.suspended << HEALTH_SUSPENDED_SHIFT;
    packed |= (uint32_t) ( diagnostics.health.write_available == 0 ) << HEALTH_WRITE_BLOCKED_SHIFT;
    packed |= ( diagnostics.fpga_failures & HEALTH_FPGA_FAILURE_MASK ) << HEALTH_FPGA_FAILURE_SHIFT;
    packed |= ( diagnostics.fpga_reconfigures & HEALTH_FPGA_RECONFIGURE_MASK )
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
    BSP_WatchdogSnapshotSet( 0, diagnostics.uptime_seconds );
    BSP_WatchdogSnapshotSet( 1, diagnostics.health.activity_count );
    BSP_WatchdogSnapshotSet( 2, packed_health() );
}

/// <summary>
///     Names the latched reason for the boot line. Anything the BSP did not
///     classify shares the "other" text with BSP_BOOT_OTHER, so the report cannot
///     tell the two apart -- deliberate, because neither is actionable and a
///     numeric fallback would invite someone to look one up.
/// </summary>
/// <returns>
///     A string literal, so it outlives every caller.
/// </returns>
static const char *boot_reason_name( void )
{
    switch ( diagnostics.boot_reason )
    {
    case BSP_BOOT_WATCHDOG:
        return "watchdog";
    case BSP_BOOT_BROWNOUT:
        return "brownout";
    case BSP_BOOT_POWER_ON:
        return "power-on";
    default:
        return "other";
    }
}

/// <summary>
///     Prints entirely from the copy start took, so the line reads identically
///     the first time and hours later when `diag` asks for it again. Nothing here
///     goes near the scratch registers, which by now hold the running loop's own
///     snapshots rather than the ones being reported.
/// </summary>
static void print_boot_report( void )
{
    BSP_ConsolePrintf( "diag: boot=%s marker=%lu loop=%lu usb=%lu health=%08lX\n",
                       boot_reason_name(), (unsigned long) diagnostics.boot_marker,
                       (unsigned long) diagnostics.boot_snapshot[ 0 ],
                       (unsigned long) diagnostics.boot_snapshot[ 1 ],
                       (unsigned long) diagnostics.boot_snapshot[ 2 ] );
}

/// <summary>
///     Every field but one comes from RAM; the marker is read back out of the
///     scratch register, so the line doubles as evidence that the retained-value
///     path still works. A marker that stops tracking the loop here means the
///     post-reset report is worthless, and this is where that shows up first.
/// </summary>
static void print_live_report( void )
{
    BSP_ConsolePrintf( "diag: t=%lus led=%u fpga_fail=%lu fpga_reconfig=%lu marker=%lu\n",
                       (unsigned long) diagnostics.uptime_seconds, diagnostics.led_on,
                       (unsigned long) diagnostics.fpga_failures,
                       (unsigned long) diagnostics.fpga_reconfigures,
                       (unsigned long) BSP_WatchdogMarkerGet() );
}

/// <summary>
///     A zero marker becomes one blink rather than none, because a code with
///     nothing to see cannot be told apart from a dead LED or a dead board. Above
///     the maximum the count saturates, so a long code reads as "eight or more"
///     rather than as an exact number somebody is expected to count.
/// </summary>
/// <returns>
///     A blink count between one and BOOT_BLINK_MAX inclusive.
/// </returns>
static uint32_t clamp_blinks( uint32_t marker )
{
    if ( marker == 0 )
    {
        return 1;
    }
    if ( marker > BOOT_BLINK_MAX )
    {
        return BOOT_BLINK_MAX;
    }
    return marker;
}

/// <summary>
///     Colour and count both carry the verdict, so a code stays readable when one
///     of them is hard to judge: the fixed counts separate the reasons an
///     onlooker cannot tell apart by hue. Only the watchdog case spends its count
///     on the retained marker, which is the one reason with more to say.
/// </summary>
/// <returns>
///     The colour and blink count standing for the latched boot reason.
/// </returns>
static boot_signature_t boot_signature( void )
{
    boot_signature_t signature = { 255, 255, 255, 1 }; /* power-on: one white blink */

    switch ( diagnostics.boot_reason )
    {
    case BSP_BOOT_WATCHDOG:
        /* red, blinked as many times as the retained progress marker */
        signature = ( boot_signature_t ){ 255, 0, 0, clamp_blinks( diagnostics.boot_marker ) };
        break;
    case BSP_BOOT_BROWNOUT:
        signature = ( boot_signature_t ){ 255, 255, 0, 2 }; /* yellow */
        break;
    case BSP_BOOT_OTHER:
        signature = ( boot_signature_t ){ 0, 255, 255, 3 }; /* cyan */
        break;
    default:
        break;
    }
    return signature;
}

/* The USB-free image has no console, so the same boot report is emitted as an
   LED blink code. This runs before the watchdog is armed, so blocking is safe. */
/// <summary>
///     Blocks for several seconds -- three passes of up to eight blinks -- which
///     only the unarmed watchdog makes safe. It repeats because there is no way
///     to ask for it again, and finishes with the LED off, so the heartbeat's
///     first write is what decides what shows next rather than a leftover colour.
/// </summary>
static void blink_boot_report( void )
{
    boot_signature_t signature = boot_signature();

    BSP_LedOff();
    BSP_TimeSleepMs( BOOT_BLINK_GAP_MS );
    for ( uint32_t pass = 0; pass < BOOT_REPORT_REPEATS; ++pass )
    {
        for ( uint32_t blink = 0; blink < signature.blinks; ++blink )
        {
            BSP_LedSet( signature.red, signature.green, signature.blue, HEARTBEAT_BRIGHTNESS );
            BSP_TimeSleepMs( BOOT_BLINK_ON_MS );
            BSP_LedOff();
            BSP_TimeSleepMs( BOOT_BLINK_OFF_MS );
        }
        BSP_TimeSleepMs( BOOT_BLINK_GAP_MS );
    }
}
