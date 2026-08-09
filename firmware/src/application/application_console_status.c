/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_console_status.h"

#include <stdint.h>

#include "application.h"
#include "application_console.h"
#include "application_console_internal.h"
#include "application_time.h"
#include "bsp.h"




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     The one place unsolicited output is produced, and it declines far more
///     often than it prints: a partially typed line, quiet mode, an unarmed
///     timer or a host that has not opened the port each suppress it. The next
///     deadline is measured from the print, so a late poll delays the following
///     line instead of bunching two of them together.
/// </summary>
void application_console_idle( void )
{
    application_console_state.current_time_ms = BSP_TimeNowMs();

    /* Unsolicited output is gated on DTR. Writing status into a port no host has
       opened is the firmware's only unbounded, self-inflicted trip through the
       untimed stdio flush loop. */
    if ( application_console_state.quiet || application_console_state.used ||
         application_console_state.status_mode == APPLICATION_CONSOLE_STATUS_DISABLED ||
         !application_deadline_reached( application_console_state.current_time_ms,
                                        application_console_state.next_status_ms ) ||
         !BSP_UsbConnected() )
    {
        return;
    }

    application_console_mark_write();
    BSP_ConsolePrintf( "\r\n" );
    application_print_status();
    application_console_print_prompt();
    application_console_state.next_status_ms =
        application_console_state.current_time_ms + application_console_state.status_period_ms;
}


/// <summary>
///     Outranks both quiet and the idle timer: it leaves quiet through the
///     composite switch rather than flag by flag, since a watch that printed
///     nothing would read as a hang and a prompt with no echo behind it is a
///     state no command can name. The mode then survives a completed command
///     line where idle status would be rescheduled from scratch, and the first
///     line falls a whole period from now, not immediately.
/// </summary>
void application_console_set_watch( uint32_t period_seconds )
{
    application_console_set_quiet( false );
    application_console_state.status_mode = APPLICATION_CONSOLE_STATUS_WATCH;
    application_console_state.status_period_ms = period_seconds * 1000u;
    application_console_state.next_status_ms =
        application_console_state.current_time_ms + application_console_state.status_period_ms;
}


/// <summary>
///     Wider than its name: it clears the automatic-status flag, so the idle
///     timer stops along with the watch and `watch off` leaves the port silent
///     until something sets the flag again. `interactive` is what does, on its
///     way through set_quiet.
/// </summary>
void application_console_disable_watch( void )
{
    application_console_state.auto_status_enabled = false;
    application_console_status_stop();
}


/// <summary>
///     Arms the first line at the idle timeout but sets the repeat to the status
///     period -- two constants that are equal today and are not the same knob.
///     Refuses to arm at all when the shell is quiet, released, or has had
///     automatic status switched off, leaving the mode disabled rather than
///     quietly deferring to a deadline nothing will honour.
/// </summary>
void application_console_status_schedule_idle( void )
{
    if ( application_console_state.quiet || application_console_state.released ||
         !application_console_state.auto_status_enabled )
    {
        application_console_state.status_mode = APPLICATION_CONSOLE_STATUS_DISABLED;
        return;
    }

    application_console_state.status_mode = APPLICATION_CONSOLE_STATUS_IDLE;
    application_console_state.status_period_ms = APPLICATION_IDLE_STATUS_PERIOD_MS;
    application_console_state.next_status_ms =
        application_console_state.current_time_ms + APPLICATION_IDLE_TIMEOUT_MS;
}


/// <summary>
///     Ends the running mode outright: the paused latch goes with it, so a
///     `quiet` or `watch off` dispatched mid-line cannot have the very line
///     that carried it resurrect the watch it just ended. The automatic-status
///     flag is untouched -- whether anything rearms later is the mode
///     switches' decision, not this one's.
/// </summary>
void application_console_status_stop( void )
{
    application_console_state.status_mode = APPLICATION_CONSOLE_STATUS_DISABLED;
    application_console_state.paused_status_mode = APPLICATION_CONSOLE_STATUS_DISABLED;
}


/// <summary>
///     What a keystroke does: silences status for the length of the line being
///     typed, latching the running mode so complete_line can put a watch back
///     with its period intact. The guard keeps the second keystroke of a line,
///     which finds the mode already disabled, from overwriting the latch with
///     the pause itself.
/// </summary>
void application_console_status_pause( void )
{
    if ( application_console_state.status_mode != APPLICATION_CONSOLE_STATUS_DISABLED )
    {
        application_console_state.paused_status_mode = application_console_state.status_mode;
    }
    application_console_state.status_mode = APPLICATION_CONSOLE_STATUS_DISABLED;
}
