/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_console.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "application.h"
#include "application_diagnostics.h"
#include "bsp.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


enum
{
    COMMAND_CAPACITY = 128
};

/* There is no boot mode here any more. Reporting status once a second until a
   key arrived used to be how the board proved it was alive; the banner the UI
   layer prints before the shell is ever entered does that job now, and does it
   in words a user who has just plugged the board in can act on. */
typedef enum
{
    STATUS_DISABLED,
    STATUS_IDLE,
    STATUS_WATCH,
} status_mode_t;

typedef struct
{
    char line[ COMMAND_CAPACITY ];
    size_t used;
    bool echo_enabled;
    bool quiet;
    bool released;
    bool auto_status_enabled;
    bool swallow_lf;
    status_mode_t status_mode;
    /* The mode a keystroke paused, so the completed line can put it back. Only
       the stop paths clear it: quiet, release and `watch off` end a watch,
       while a keystroke merely holds it for the length of a line. */
    status_mode_t paused_status_mode;
    uint32_t current_time_ms;
    uint32_t next_status_ms;
    uint32_t status_period_ms;
} console_state_t;




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


static console_state_t console;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bool deadline_reached( uint32_t now_ms, uint32_t deadline_ms );

static void mark_write( void );

static void print_prompt( void );

static void schedule_idle_status( void );

static void stop_active_status( void );

static void pause_active_status( void );

static void echo_character( int16_t character );

static void erase_character( void );

static void complete_line( void );

static void cancel_line( void );

static void redraw_line( void );

static void process_character( int16_t character );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Reassigns the whole state, which is what makes a second call the way the
///     UI hands the terminal back after a menu: the half-typed line, the quiet
///     flag and the released flag all go with it. The clock is sampled here
///     rather than carried over, so the first idle status falls one timeout
///     after the handover and not one timeout after boot.
/// </summary>
void application_console_start( void )
{
    console = ( console_state_t ){
        .echo_enabled = true,
        .auto_status_enabled = true,
    };
    console.current_time_ms = BSP_TimeNowMs();
    schedule_idle_status();
    print_prompt();
}


/// <summary>
///     The shell's only input path: one character, already read by the UI, with
///     no lookahead and no way to push a byte back. Every terminal behaviour
///     below -- editing, cancelling, submitting -- is therefore decided one
///     character at a time, and the clock is restamped first so that time in
///     this layer advances only when the UI reports something arrived.
/// </summary>
void application_console_feed( int16_t character )
{
    console.current_time_ms = BSP_TimeNowMs();
    process_character( character );
}


/// <summary>
///     The one place unsolicited output is produced, and it declines far more
///     often than it prints: a partially typed line, quiet mode, an unarmed
///     timer or a host that has not opened the port each suppress it. The next
///     deadline is measured from the print, so a late poll delays the following
///     line instead of bunching two of them together.
/// </summary>
void application_console_idle( void )
{
    console.current_time_ms = BSP_TimeNowMs();

    /* Unsolicited output is gated on DTR. Writing status into a port no host has
       opened is the firmware's only unbounded, self-inflicted trip through the
       untimed stdio flush loop. */
    if ( console.quiet || console.used || console.status_mode == STATUS_DISABLED ||
         !deadline_reached( console.current_time_ms, console.next_status_ms ) ||
         !BSP_UsbConnected() )
    {
        return;
    }

    mark_write();
    BSP_ConsolePrintf( "\r\n" );
    application_print_status();
    print_prompt();
    console.next_status_ms = console.current_time_ms + console.status_period_ms;
}


/// <summary>
///     One-way: nothing here clears the flag again, so application_console_start
///     is the only route back to owning the terminal. The buffered line and the
///     echo and quiet settings are left standing -- releasing silences the
///     shell, it does not reset it.
/// </summary>
void application_console_release( void )
{
    console.released = true;
    stop_active_status();
}


/// <summary>
///     Local rendering only: with echo off a command still runs, it just leaves
///     no trace of itself on the screen. Not an independent setting either --
///     set_quiet writes this same flag, so an explicit `echo on` lasts only
///     until the next quiet, interactive, or watch switch.
/// </summary>
void application_console_set_echo( bool enabled )
{
    console.echo_enabled = enabled;
}


/// <summary>
///     The composite switch a script wants: echo and automatic status are both
///     derived from this one flag, so nothing but command output reaches the
///     port. Either direction disarms the running timer, which means leaving
///     quiet mode does not resume status until the next completed line
///     reschedules it.
/// </summary>
void application_console_set_quiet( bool enabled )
{
    console.quiet = enabled;
    console.echo_enabled = !enabled;
    console.auto_status_enabled = !enabled;
    stop_active_status();
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
    console.status_mode = STATUS_WATCH;
    console.status_period_ms = period_seconds * 1000u;
    console.next_status_ms = console.current_time_ms + console.status_period_ms;
}


/// <summary>
///     Wider than its name: it clears the automatic-status flag, so the idle
///     timer stops along with the watch and `watch off` leaves the port silent
///     until something sets the flag again. `interactive` is what does, on its
///     way through set_quiet.
/// </summary>
void application_console_disable_watch( void )
{
    console.auto_status_enabled = false;
    stop_active_status();
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/// <summary>
///     Compares by signed difference rather than by magnitude, which is what
///     makes the millisecond clock's 49-day rollover a non-event: a plain
///     now >= deadline would answer "not yet" for half the counter's range once
///     it has wrapped, stalling every timer in the shell at once.
/// </summary>
/// <returns>
///     True once now is at or past the deadline, wrap included.
/// </returns>
static bool deadline_reached( uint32_t now_ms, uint32_t deadline_ms )
{
    return (int32_t) ( now_ms - deadline_ms ) >= 0;
}


/* Every console write reaches the untimed Pico SDK stdio flush loop, so the
   marker is set immediately before the call. After a watchdog reset the
   retained marker names the path the foreground was blocked in. */
/// <summary>
///     Deliberately has no matching clear. The next foreground iteration
///     overwrites the marker, so finding this one still in place after a reset
///     means the write never came back. It precedes every console write in this
///     file, prompt and echo included, which is why it stays a single store.
/// </summary>
static void mark_write( void )
{
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE );
}


/// <summary>
///     Emits no newline of its own, so every caller has to have left the cursor
///     at the start of a line first. Silent once released, which is what keeps a
///     dismissed shell from claiming the screen back underneath the menu that
///     replaced it.
/// </summary>
static void print_prompt( void )
{
    if ( !console.quiet && !console.released )
    {
        mark_write();
        BSP_ConsolePrintf( "forgix> " );
    }
}


/// <summary>
///     Arms the first line at the idle timeout but sets the repeat to the status
///     period -- two constants that are equal today and are not the same knob.
///     Refuses to arm at all when the shell is quiet, released, or has had
///     automatic status switched off, leaving the mode disabled rather than
///     quietly deferring to a deadline nothing will honour.
/// </summary>
static void schedule_idle_status( void )
{
    if ( console.quiet || console.released || !console.auto_status_enabled )
    {
        console.status_mode = STATUS_DISABLED;
        return;
    }

    console.status_mode = STATUS_IDLE;
    console.status_period_ms = APPLICATION_IDLE_STATUS_PERIOD_MS;
    console.next_status_ms = console.current_time_ms + APPLICATION_IDLE_TIMEOUT_MS;
}


/// <summary>
///     Ends the running mode outright: the paused latch goes with it, so a
///     `quiet` or `watch off` dispatched mid-line cannot have the very line
///     that carried it resurrect the watch it just ended. The automatic-status
///     flag is untouched -- whether anything rearms later is the mode
///     switches' decision, not this one's.
/// </summary>
static void stop_active_status( void )
{
    console.status_mode = STATUS_DISABLED;
    console.paused_status_mode = STATUS_DISABLED;
}


/// <summary>
///     What a keystroke does: silences status for the length of the line being
///     typed, latching the running mode so complete_line can put a watch back
///     with its period intact. The guard keeps the second keystroke of a line,
///     which finds the mode already disabled, from overwriting the latch with
///     the pause itself.
/// </summary>
static void pause_active_status( void )
{
    if ( console.status_mode != STATUS_DISABLED )
    {
        console.paused_status_mode = console.status_mode;
    }
    console.status_mode = STATUS_DISABLED;
}


/// <summary>
///     Also the bell path: a rejected keystroke is reported by passing '\a'
///     through here, so with echo off or quiet set the rejection is silent
///     rather than merely unmirrored. That is intended -- a port being driven by
///     a script has nobody there to hear it.
/// </summary>
static void echo_character( int16_t character )
{
    if ( !console.quiet && console.echo_enabled )
    {
        mark_write();
        BSP_ConsolePutChar( (uint8_t) character );
    }
}


/// <summary>
///     Backspace, space, backspace: a lone backspace moves the cursor without
///     removing the glyph under it. Display only -- the caller has already
///     shortened the buffer, so the two have to stay paired or the screen and
///     the line stop agreeing about what was typed.
/// </summary>
static void erase_character( void )
{
    if ( !console.quiet && console.echo_enabled )
    {
        mark_write();
        BSP_ConsolePrintf( "\b \b" );
    }
}


/// <summary>
///     Dispatch carries its own progress marker and happens before the next
///     prompt, so a command's output lands above that prompt and a hang inside
///     one is attributed to the command rather than to the write before it. An
///     empty line is not an error, just a fresh prompt, and a running watch is
///     left alone where idle status would be rescheduled.
/// </summary>
static void complete_line( void )
{
    if ( !console.quiet && console.echo_enabled )
    {
        mark_write();
        BSP_ConsolePrintf( "\r\n" );
    }

    if ( console.used )
    {
        console.line[ console.used ] = 0;
        BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_COMMAND );
        application_process_command( console.line );
        console.used = 0;
    }

    /* The keystroke that started this line paused whatever was running. A
       command that armed its own watch outranks the restore; otherwise a
       paused watch resumes with its period intact, one whole period from the
       line that interrupted it, and only the idle default starts over. */
    if ( console.status_mode != STATUS_WATCH )
    {
        if ( console.paused_status_mode == STATUS_WATCH )
        {
            console.status_mode = STATUS_WATCH;
            console.next_status_ms = console.current_time_ms + console.status_period_ms;
        }
        else
        {
            schedule_idle_status();
        }
    }
    console.paused_status_mode = STATUS_DISABLED;
    print_prompt();
}


/// <summary>
///     Drops the buffer without dispatching it and prints ^C, so an abandoned
///     line is visibly abandoned rather than just gone. Unlike a completed line
///     this always falls back to idle status, which means Ctrl-C also ends a
///     running watch as a side effect of cancelling whatever was typed.
/// </summary>
static void cancel_line( void )
{
    console.used = 0;
    if ( !console.quiet )
    {
        mark_write();
        BSP_ConsolePrintf( "^C\r\n" );
    }
    stop_active_status();
    schedule_idle_status();
    print_prompt();
}


/// <summary>
///     Repaints onto a new line instead of clearing the screen: the job is to
///     recover a line that unsolicited output has scrolled through, not to hide
///     what that output said. Printed with an explicit length because the buffer
///     only gains its terminator when the line completes.
/// </summary>
static void redraw_line( void )
{
    if ( !console.quiet )
    {
        mark_write();
        BSP_ConsolePrintf( "\r\nforgix> %.*s", (int) console.used, console.line );
    }
}


/// <summary>
///     One Enter is one line: the swallow flag eats the LF of a CRLF pair, and
///     any other character clears it so a late LF cannot swallow the next
///     command's newline instead. Status is paused -- not stopped -- before
///     anything else is decided, so typing silences a watch without ending it,
///     and the buffer always holds a byte back for the terminator, so a full
///     line rings rather than truncating. Unhandled control codes are dropped
///     in silence.
/// </summary>
static void process_character( int16_t character )
{
    if ( character == '\n' && console.swallow_lf )
    {
        console.swallow_lf = false;
        return;
    }
    console.swallow_lf = false;
    pause_active_status();

    if ( character == '\r' || character == '\n' )
    {
        console.swallow_lf = character == '\r';
        complete_line();
    }
    else if ( character == 3 )
    {
        cancel_line();
    }
    else if ( character == 12 )
    {
        redraw_line();
    }
    else if ( character == 21 )
    {
        while ( console.used )
        {
            --console.used;
            erase_character();
        }
    }
    else if ( character == '\b' || character == 127 )
    {
        if ( console.used )
        {
            --console.used;
            erase_character();
        }
        else
        {
            echo_character( '\a' );
        }
    }
    else if ( isprint( (unsigned char) character ) )
    {
        if ( console.used + 1 < sizeof console.line )
        {
            console.line[ console.used++ ] = (char) character;
            echo_character( character );
        }
        else
        {
            echo_character( '\a' );
        }
    }
}
