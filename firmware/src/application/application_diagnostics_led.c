/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_diagnostics_led.h"

#include <stdbool.h>
#include <stdint.h>

#include "application_diagnostics.h"
#include "application_diagnostics_internal.h"
#include "application_time.h"
#include "bsp.h"




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static void resting_color( uint8_t *red, uint8_t *green, uint8_t *blue );

static void heartbeat_color( uint32_t now_ms, uint8_t *red, uint8_t *green, uint8_t *blue );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     The only place the heartbeat touches the LED, and it records what it asked
///     for in the same step -- the FPGA readback check has nothing else to
///     compare against, so a write that bypassed this would be reported as a bus
///     fault. The dark half clears only the enable flag, matching BSP_LedOff,
///     which leaves the recorded colour still describing the registers.
/// </summary>
void application_diagnostics_apply_led( uint32_t now_ms )
{
    if ( application_diagnostics_state.led_on )
    {
        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;
        heartbeat_color( now_ms, &red, &green, &blue );
        BSP_LedSet( red, green, blue, APPLICATION_DIAGNOSTICS_HEARTBEAT_BRIGHTNESS );
        application_diagnostics_state.commanded = ( bsp_led_state_t ){
            .red = red,
            .green = green,
            .blue = blue,
            .brightness = APPLICATION_DIAGNOSTICS_HEARTBEAT_BRIGHTNESS,
            .enabled = true,
        };
    }
    else
    {
        BSP_LedOff();
        application_diagnostics_state.commanded.enabled = false;
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
bool application_diagnostics_led_readback_matches( void )
{
    bsp_led_state_t led = BSP_LedGet();
    return led.red == application_diagnostics_state.commanded.red &&
           led.green == application_diagnostics_state.commanded.green &&
           led.blue == application_diagnostics_state.commanded.blue &&
           led.brightness == application_diagnostics_state.commanded.brightness &&
           led.enabled == application_diagnostics_state.commanded.enabled;
}

/// <summary>
///     Stands down the heartbeat write and the readback comparison together, and
///     deliberately leaves the LED showing whatever it last commanded: the new
///     owner inherits a lit board rather than a dark one, and inherits it before
///     it has painted anything of its own.
/// </summary>
void application_diagnostics_release_led( void )
{
    application_diagnostics_state.led_released = true;
}

/// <summary>
///     Resumes the heartbeat at whatever phase it would have reached, not at the
///     start of a period, so a short light show does not visibly reset the blink.
///     Harmless without a matching release, which is what lets an aborted
///     activity's stop path call it unconditionally.
/// </summary>
void application_diagnostics_reclaim_led( void )
{
    application_diagnostics_state.led_released = false;
    /* Written immediately rather than at the next 250 ms edge. Waiting would
       leave whatever the last owner painted on the board for a quarter of a
       second after it stopped owning it, and -- worse -- would leave the FPGA
       health check comparing against a command that predates the handover if it
       samples first. */
    application_diagnostics_apply_led( BSP_TimeNowMs() );
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


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

    switch ( application_diagnostics_state.boot_reason )
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
    if ( application_diagnostics_state.recovery_toggles )
    {
        *red = 255;
        *green = 255;
        *blue = 255; /* white: FPGA reconfiguration recovery signature */
    }
    else if ( !application_diagnostics_state.usb_present )
    {
        resting_color( red, green, blue );
    }
    else if ( !application_diagnostics_state.health.connected )
    {
        *red = 0;
        *green = 0;
        *blue = 255; /* blue: host has not asserted DTR */
    }
    else if ( application_diagnostics_state.health.suspended ||
              application_stalled_since( now_ms, application_diagnostics_state.last_frame_ms,
                                         APPLICATION_DIAGNOSTICS_FRAME_STALL_MS ) )
    {
        *red = 255;
        *green = 0;
        *blue = 255; /* magenta: bus suspended or start-of-frame counter frozen */
    }
    else if ( application_diagnostics_state.fifo_stalled &&
              application_stalled_since( now_ms, application_diagnostics_state.fifo_stall_epoch_ms,
                                         APPLICATION_DIAGNOSTICS_FIFO_STALL_MS ) )
    {
        /* red: every sample for the whole window saw the transmit FIFO full
           with no TX completion, measured from the first such sample. The flag
           already encodes "full at the last sample", so no separate
           write_available test is needed here. Two earlier shapes of this
           verdict were wrong: keying off the last CDC traffic punished quiet
           links, and pooling RX with TX let inbound traffic conceal a wedged
           transmit endpoint indefinitely. */
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
