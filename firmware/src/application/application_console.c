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
#include "application_console_internal.h"
#include "application_console_status.h"
#include "application_diagnostics.h"
#include "bsp.h"




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* Not static: application_console_status.c decides whether to print from the
   same state the editor writes -- a half-typed line, the quiet flag and the
   released flag all suppress unsolicited output -- and reaches it through the
   extern in application_console_internal.h. The two files are one module split
   by concern, so they share the singleton rather than each keeping half. */
console_state_t console;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


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
    application_console_status_schedule_idle();
    application_console_print_prompt();
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
///     One-way: nothing here clears the flag again, so application_console_start
///     is the only route back to owning the terminal. The buffered line and the
///     echo and quiet settings are left standing -- releasing silences the
///     shell, it does not reset it.
/// </summary>
void application_console_release( void )
{
    console.released = true;
    application_console_status_stop();
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
    application_console_status_stop();
}


/* Every console write reaches the untimed Pico SDK stdio flush loop, so the
   marker is set immediately before the call. After a watchdog reset the
   retained marker names the path the foreground was blocked in. */
/// <summary>
///     Deliberately has no matching clear. The next foreground iteration
///     overwrites the marker, so finding this one still in place after a reset
///     means the write never came back. It precedes every console write in both
///     halves of the shell, prompt and echo included, which is why it stays a
///     single store.
/// </summary>
void application_console_mark_write( void )
{
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE );
}


/// <summary>
///     Emits no newline of its own, so every caller has to have left the cursor
///     at the start of a line first. Silent once released, which is what keeps a
///     dismissed shell from claiming the screen back underneath the menu that
///     replaced it. Shared with the status scheduler, which reprints it under
///     every unsolicited line it emits.
/// </summary>
void application_console_print_prompt( void )
{
    if ( !console.quiet && !console.released )
    {
        application_console_mark_write();
        BSP_ConsolePrintf( "forgix> " );
    }
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


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
        application_console_mark_write();
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
        application_console_mark_write();
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
        application_console_mark_write();
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
    if ( console.status_mode != APPLICATION_CONSOLE_STATUS_WATCH )
    {
        if ( console.paused_status_mode == APPLICATION_CONSOLE_STATUS_WATCH )
        {
            console.status_mode = APPLICATION_CONSOLE_STATUS_WATCH;
            console.next_status_ms = console.current_time_ms + console.status_period_ms;
        }
        else
        {
            application_console_status_schedule_idle();
        }
    }
    console.paused_status_mode = APPLICATION_CONSOLE_STATUS_DISABLED;
    application_console_print_prompt();
}


/// <summary>
///     Drops the buffer without dispatching it and prints ^C, so an abandoned
///     line is visibly abandoned rather than just gone. The marker is
///     echo-class output -- it mirrors what was typed -- so echo off silences
///     it the same way it silences the characters it stands for. Unlike a
///     completed line this always falls back to idle status, which means
///     Ctrl-C also ends a running watch as a side effect of cancelling
///     whatever was typed.
/// </summary>
static void cancel_line( void )
{
    console.used = 0;
    if ( !console.quiet && console.echo_enabled )
    {
        application_console_mark_write();
        BSP_ConsolePrintf( "^C\r\n" );
    }
    application_console_status_stop();
    application_console_status_schedule_idle();
    application_console_print_prompt();
}


/// <summary>
///     Repaints onto a new line instead of clearing the screen: the job is to
///     recover a line that unsolicited output has scrolled through, not to
///     hide what that output said. With echo off there is nothing on screen to
///     recover, so the repaint is withheld like the echo it would restore.
///     Printed with an explicit length because the buffer only gains its
///     terminator when the line completes.
/// </summary>
static void redraw_line( void )
{
    if ( !console.quiet && console.echo_enabled )
    {
        application_console_mark_write();
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
    application_console_status_pause();

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
