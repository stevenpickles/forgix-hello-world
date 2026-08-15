/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_diagnostics_report.h"

#include <stdint.h>

#include "application_diagnostics.h"
#include "application_diagnostics_internal.h"
#include "bsp.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


enum
{
    BOOT_BLINK_MAX = 8,
    /* The blink code is the USB-free image's only boot-evidence channel, and it
       plays once: a power cycle to see it again would destroy the very scratch
       registers it is reporting. So it is paced to be readable and repeated. */
    BOOT_BLINK_ON_MS = 350,
    BOOT_BLINK_OFF_MS = 250,
    BOOT_BLINK_GAP_MS = 800,
    BOOT_REPORT_REPEATS = 3,
};


typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint32_t blinks;
} boot_signature_t;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static const char *boot_reason_name( void );

static uint32_t clamp_blinks( uint32_t marker );

static boot_signature_t boot_signature( void );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Replays the boot line from what start captured rather than re-reading the
///     hardware, which by now describes this run, then adds the live counters at
///     their full width -- the retained slots only carry them modulo the bit
///     fields they were packed into.
/// </summary>
void application_diagnostics_print_report( void )
{
    application_diagnostics_report_boot();
    BSP_ConsolePrintf(
        "diag: uptime=%lus connected=%u suspended=%u write=%lu activity=%lu sof=%lu "
        "fpga_fail=%lu fpga_cdone=%lu fpga_ping=%lu fpga_led=%lu fpga_reconfig=%lu\n",
        (unsigned long) application_diagnostics_state.uptime_seconds,
        (unsigned) application_diagnostics_state.health.connected,
        (unsigned) application_diagnostics_state.health.suspended,
        (unsigned long) application_diagnostics_state.health.write_available,
        (unsigned long) application_diagnostics_state.health.activity_count,
        (unsigned long) application_diagnostics_state.health.frame_number,
        (unsigned long) application_diagnostics_state.fpga_failures,
        (unsigned long) application_diagnostics_state.fpga_cdone_failures,
        (unsigned long) application_diagnostics_state.fpga_ping_failures,
        (unsigned long) application_diagnostics_state.fpga_readback_failures,
        (unsigned long) application_diagnostics_state.fpga_reconfigures );
}

/// <summary>
///     Prints entirely from the copy start took, so the line reads identically
///     the first time and hours later when `diag` asks for it again. Nothing here
///     goes near the scratch registers, which by now hold the running loop's own
///     snapshots rather than the ones being reported.
/// </summary>
void application_diagnostics_report_boot( void )
{
    BSP_ConsolePrintf( "diag: boot=%s marker=%lu loop=%lu usb=%lu health=%08lX\n",
                       boot_reason_name(),
                       (unsigned long) application_diagnostics_state.boot_marker,
                       (unsigned long) application_diagnostics_state.boot_snapshot[ 0 ],
                       (unsigned long) application_diagnostics_state.boot_snapshot[ 1 ],
                       (unsigned long) application_diagnostics_state.boot_snapshot[ 2 ] );
}

/// <summary>
///     Every field but one comes from RAM; the marker is read back out of the
///     scratch register, so the line doubles as evidence that the retained-value
///     path still works. A marker that stops tracking the loop here means the
///     post-reset report is worthless, and this is where that shows up first.
/// </summary>
void application_diagnostics_report_live( void )
{
    BSP_ConsolePrintf( "diag: t=%lus led=%u fpga_fail=%lu fpga_reconfig=%lu marker=%lu\n",
                       (unsigned long) application_diagnostics_state.uptime_seconds,
                       application_diagnostics_state.led_on,
                       (unsigned long) application_diagnostics_state.fpga_failures,
                       (unsigned long) application_diagnostics_state.fpga_reconfigures,
                       (unsigned long) BSP_WatchdogMarkerGet() );
}

/* The USB-free image has no console, so the same boot report is emitted as an
   LED blink code. The BSP's early watchdog is live, so each complete pass feeds
   it before the next pass can begin. */
/// <summary>
///     Blocks for several seconds -- three passes of up to eight blinks -- which
///     remains safe because each pass is shorter than the early watchdog window
///     and feeds it. It repeats because there is no way to ask for it again, and
///     finishes with the LED off, so the heartbeat's first write decides what
///     shows next rather than a leftover colour.
/// </summary>
void application_diagnostics_report_blink( void )
{
    boot_signature_t signature = boot_signature();

    BSP_LedOff();
    BSP_TimeSleepMs( BOOT_BLINK_GAP_MS );
    for ( uint32_t pass = 0; pass < BOOT_REPORT_REPEATS; ++pass )
    {
        for ( uint32_t blink = 0; blink < signature.blinks; ++blink )
        {
            BSP_LedSet( signature.red, signature.green, signature.blue,
                        APPLICATION_DIAGNOSTICS_HEARTBEAT_BRIGHTNESS );
            BSP_TimeSleepMs( BOOT_BLINK_ON_MS );
            BSP_LedOff();
            BSP_TimeSleepMs( BOOT_BLINK_OFF_MS );
        }
        BSP_TimeSleepMs( BOOT_BLINK_GAP_MS );
        BSP_WatchdogFeed();
    }
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


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
    switch ( application_diagnostics_state.boot_reason )
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

    switch ( application_diagnostics_state.boot_reason )
    {
    case BSP_BOOT_WATCHDOG:
        /* red, blinked as many times as the retained progress marker */
        signature = ( boot_signature_t ){
            255, 0, 0, clamp_blinks( application_diagnostics_state.boot_marker ) };
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
