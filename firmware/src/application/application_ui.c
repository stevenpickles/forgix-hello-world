/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_ui.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "application_console.h"
#include "application_diagnostics.h"
#include "application_time.h"
#include "application_ui_internal.h"
#include "application_ui_menu.h"
#include "bsp.h"




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* Not static: application_ui_menu.c writes the mode it is switching into and
   reads the clock this pass cached, and it does so through the extern in
   application_ui_internal.h. The two files are one module split by concern, so
   they share the singleton rather than each keeping half of it. */
ui_state_t application_ui_state;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static void print_banner( void );

static void enter_menu( void );

static void finish_activity( void );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Reassigns the whole state struct rather than just the mode, so a restart
///     cannot leave a stale activity pointer behind for the poll to call through.
///     The first banner is left already due, so a host that has just opened the
///     port sees a line at once instead of after a full period of silence.
/// </summary>
void application_ui_start( void )
{
    application_ui_state = ( ui_state_t ){ .mode = APPLICATION_UI_MODE_BANNER };
    application_ui_state.current_time_ms = BSP_TimeNowMs();
    application_ui_state.started_ms = application_ui_state.current_time_ms;
    application_ui_state.next_banner_ms = application_ui_state.current_time_ms;
}


/// <summary>
///     Takes the terminal back from the shell, and is reached from inside command
///     dispatch -- so ownership changes hands part-way through a command and the
///     shell must print nothing after this returns. A wrapper, so the internal
///     enter_menu need not be published to reach it.
/// </summary>
void application_ui_enter_menu( void )
{
    enter_menu();
}


/// <summary>
///     One pass of the foreground loop. Reads at most one character, with the 1 ms
///     timeout that paces the whole loop, and gives it to whoever owns the terminal
///     in the current mode. Shell and activity modes consume it and return, so the
///     banner clock only advances while nothing else owns the screen.
/// </summary>
void application_ui_poll( void )
{
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_READ );
    int16_t character = BSP_ConsoleGetCharTimeoutUs( 1000 );
    application_ui_state.current_time_ms = BSP_TimeNowMs();

    /* A real byte is 0..255; the timeout sentinel is merely the common
       negative. Testing against the sentinel alone would let any other
       negative SDK error code impersonate a keystroke -- and a phantom
       keystroke aborts activities and dismisses banners. */
    if ( application_ui_state.mode == APPLICATION_UI_MODE_SHELL )
    {
        if ( character >= 0 )
        {
            application_console_feed( character );
        }
        else
        {
            application_console_idle();
        }
        return;
    }

    if ( application_ui_state.mode == APPLICATION_UI_MODE_ACTIVITY )
    {
        /* Any key aborts. A user watching a test they no longer want should not
           have to remember which key means stop. */
        if ( character >= 0 )
        {
            application_ui_state.activity->stop();
            application_ui_mark_write();
            BSP_ConsolePrintf( "\naborted\n" );
            finish_activity();
        }
        else if ( !application_ui_state.activity->poll() )
        {
            finish_activity();
        }
        return;
    }

    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_MENU );

    if ( character >= 0 )
    {
        if ( application_ui_state.mode == APPLICATION_UI_MODE_BANNER )
        {
            /* The key that ends the banner is consumed by ending it. Treating it
               as a selection as well would fire whichever item the user happened
               to hit while reaching for any key at all. */
            enter_menu();
        }
        else if ( application_ui_state.mode == APPLICATION_UI_MODE_STEPS )
        {
            application_ui_menu_select_step( character );
        }
        else
        {
            application_ui_menu_select_entry( character );
        }
        return;
    }

    if ( application_ui_state.mode != APPLICATION_UI_MODE_BANNER ||
         !application_deadline_reached( application_ui_state.current_time_ms,
                                        application_ui_state.next_banner_ms ) )
    {
        return;
    }

    application_ui_state.next_banner_ms =
        application_ui_state.current_time_ms + APPLICATION_UI_BANNER_PERIOD_MS;
    ++application_ui_state.banner_count;

    /* The count advances whether or not anyone is listening, so it reads as
       uptime rather than as a byte count. Only the writing is gated on DTR:
       pushing into a port no host has opened is the one unbounded trip through
       the untimed stdio flush loop this firmware can inflict on itself. */
    if ( BSP_UsbConnected() )
    {
        print_banner();
    }
}


/* Every console write reaches the untimed Pico SDK stdio flush loop, so the
   marker is set immediately before the call, exactly as the shell does. */
/// <summary>
///     Claims the console-write marker for the line about to be printed, per call
///     rather than per function, so a board that stops inside the flush leaves a
///     marker naming the individual write rather than the menu as a whole. Shared
///     with application_ui_menu.c, which does the bulk of the writing.
/// </summary>
void application_ui_mark_write( void )
{
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE );
}


/// <summary>
///     Seconds since the UI started, which is not the same as since boot -- bring-up
///     runs before this. Computed from the stamp the current pass cached, so every
///     line of one menu draw agrees on the figure.
/// </summary>
/// <returns>
///     Whole seconds, truncated.
/// </returns>
uint32_t application_ui_uptime_seconds( void )
{
    return ( application_ui_state.current_time_ms - application_ui_state.started_ms ) / 1000u;
}


/* An activity owns the LED for its whole run, not just the parts that paint it.
   The heartbeat is a 2 Hz writer and every activity here holds a colour for
   longer than that, so sharing the LED means the heartbeat showing through the
   middle of whatever the activity was trying to display. */
/// <summary>
///     Hands over the LED and the terminal in one move, then runs the activity's
///     start() inline: anything it prints appears before this returns, and the first
///     poll() does not come until the next pass of the loop. The menu entries are
///     the only callers, which is why this leaves the file the mode machine is in.
/// </summary>
void application_ui_start_activity( const application_activity_t *activity )
{
    application_diagnostics_release_led();
    application_ui_state.mode = APPLICATION_UI_MODE_ACTIVITY;
    application_ui_state.activity = activity;
    activity->start();
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/// <summary>
///     Writes one banner line unconditionally. Whether a host is there to read it
///     is the caller's decision, and the poll is the only place that asks -- pushing
///     into a port nobody has opened is what the DTR check exists to prevent.
/// </summary>
static void print_banner( void )
{
    application_ui_mark_write();
    BSP_ConsolePrintf( "hello world - %lu - press any key\n",
                       (unsigned long) application_ui_state.banner_count );
}


/// <summary>
///     The one place the terminal comes back to the UI -- from the banner, from the
///     shell, from a finished activity alike. It always redraws, so no caller has to
///     work out whether what is still on screen is usable.
/// </summary>
static void enter_menu( void )
{
    /* Unconditional, including on paths where the shell was never started. The
       `menu` command reaches here from inside command dispatch, and the shell
       would otherwise print one last prompt after the menu that replaced it. */
    application_console_release();
    application_ui_state.mode = APPLICATION_UI_MODE_MENU;
    application_ui_menu_print();
}


/// <summary>
///     The mirror of application_ui_start_activity -- drops the activity, takes the
///     LED back and redraws. It deliberately does not call stop(): an activity that
///     ended of its own accord has already tidied up, and the abort path calls stop()
///     before reaching here, so calling it would run cleanup twice.
/// </summary>
static void finish_activity( void )
{
    application_ui_state.activity = NULL;
    application_diagnostics_reclaim_led();
    enter_menu();
}
