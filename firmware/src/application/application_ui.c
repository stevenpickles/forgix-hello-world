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
#include "application_effects.h"
#include "application_ibit.h"
#include "bsp.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


typedef enum
{
    UI_MODE_BANNER,
    UI_MODE_MENU,
    UI_MODE_STEPS,
    UI_MODE_ACTIVITY,
    UI_MODE_SHELL,
} ui_mode_t;


typedef struct
{
    ui_mode_t mode;
    uint32_t current_time_ms;
    uint32_t next_banner_ms;
    uint32_t banner_count;
    uint32_t started_ms;
    const application_activity_t *activity;
} ui_state_t;


typedef struct
{
    char key;
    const char *label;
    const char *detail;
    void ( *action )( void );
} menu_entry_t;




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


static ui_state_t ui;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bool deadline_reached( uint32_t now_ms, uint32_t deadline_ms );

static void mark_write( void );

static uint32_t uptime_seconds( void );

static void print_banner( void );

static void print_menu( void );

static char step_key( uint32_t index );

static void print_steps( void );

static void enter_menu( void );

static void start_activity( const application_activity_t *activity );

static void finish_activity( void );

static void action_redraw( void );

static void action_ibit( void );

static void action_soak( void );

static void action_steps( void );

static void action_report( void );

static void action_blinker( void );

static void action_advanced( void );

static void action_shell( void );

static void action_reboot( void );

static void action_bootsel( void );

static void select_entry( int16_t character );

static void select_step( int16_t character );

/* Takes the address of the ten action_* handlers above; C requires a
   function be declared before its address is taken, so this table follows
   the prototypes it binds instead of sitting under Private Variable
   Declarations with the rest of the module's data. One table drives both
   the rendering and the dispatch, so a key can never be offered without
   doing something or do something without being offered. */
static const menu_entry_t MENU[] = {
    { '1', "Built-in test", "the whole sequence, once", action_ibit },
    { '2', "Built-in test soak", "repeat with a tally until a key is pressed", action_soak },
    { '3', "One test at a time", "re-run a single step without the other thirteen", action_steps },
    { '4', "Board report", "what this board is, without judging it", action_report },
    { '5', "Blinker", "red, green, blue at 1 Hz until a key is pressed", action_blinker },
    { '6', "Advanced blinker", "heartbeat, colour wheel, aurora", action_advanced },
    { 'c', "Command shell", "the forgix> prompt; `menu` returns here", action_shell },
    { 'r', "Reboot", "restart the board and reconfigure the FPGA", action_reboot },
    { 'b', "Reboot to BOOTSEL", "hand the board to the USB loader for reflashing", action_bootsel },
    { '?', "Redraw this menu", "", action_redraw },
};




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
    ui = ( ui_state_t ){ .mode = UI_MODE_BANNER };
    ui.current_time_ms = BSP_TimeNowMs();
    ui.started_ms = ui.current_time_ms;
    ui.next_banner_ms = ui.current_time_ms;
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
    ui.current_time_ms = BSP_TimeNowMs();

    if ( ui.mode == UI_MODE_SHELL )
    {
        if ( character != BSP_CONSOLE_TIMEOUT )
        {
            application_console_feed( character );
        }
        else
        {
            application_console_idle();
        }
        return;
    }

    if ( ui.mode == UI_MODE_ACTIVITY )
    {
        /* Any key aborts. A user watching a test they no longer want should not
           have to remember which key means stop. */
        if ( character != BSP_CONSOLE_TIMEOUT )
        {
            ui.activity->stop();
            mark_write();
            BSP_ConsolePrintf( "\naborted\n" );
            finish_activity();
        }
        else if ( !ui.activity->poll() )
        {
            finish_activity();
        }
        return;
    }

    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_MENU );

    if ( character != BSP_CONSOLE_TIMEOUT )
    {
        if ( ui.mode == UI_MODE_BANNER )
        {
            /* The key that ends the banner is consumed by ending it. Treating it
               as a selection as well would fire whichever item the user happened
               to hit while reaching for any key at all. */
            enter_menu();
        }
        else if ( ui.mode == UI_MODE_STEPS )
        {
            select_step( character );
        }
        else
        {
            select_entry( character );
        }
        return;
    }

    if ( ui.mode != UI_MODE_BANNER || !deadline_reached( ui.current_time_ms, ui.next_banner_ms ) )
    {
        return;
    }

    ui.next_banner_ms = ui.current_time_ms + APPLICATION_UI_BANNER_PERIOD_MS;
    ++ui.banner_count;

    /* The count advances whether or not anyone is listening, so it reads as
       uptime rather than as a byte count. Only the writing is gated on DTR:
       pushing into a port no host has opened is the one unbounded trip through
       the untimed stdio flush loop this firmware can inflict on itself. */
    if ( BSP_UsbConnected() )
    {
        print_banner();
    }
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/// <summary>
///     Compares millisecond stamps through a signed difference, so the banner keeps
///     its cadence across the 32-bit rollover instead of falling silent for the
///     remainder of the wrap.
/// </summary>
/// <returns>
///     True once now_ms has reached deadline_ms.
/// </returns>
static bool deadline_reached( uint32_t now_ms, uint32_t deadline_ms )
{
    return (int32_t) ( now_ms - deadline_ms ) >= 0;
}


/* Every console write reaches the untimed Pico SDK stdio flush loop, so the
   marker is set immediately before the call, exactly as the shell does. */
/// <summary>
///     Claims the console-write marker for the line about to be printed, per call
///     rather than per function, so a board that stops inside the flush leaves a
///     marker naming the individual write rather than the menu as a whole.
/// </summary>
static void mark_write( void )
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
static uint32_t uptime_seconds( void )
{
    return ( ui.current_time_ms - ui.started_ms ) / 1000u;
}


/// <summary>
///     Writes one banner line unconditionally. Whether a host is there to read it
///     is the caller's decision, and the poll is the only place that asks -- pushing
///     into a port nobody has opened is what the DTR check exists to prevent.
/// </summary>
static void print_banner( void )
{
    mark_write();
    BSP_ConsolePrintf( "hello world - %lu - press any key\n", (unsigned long) ui.banner_count );
}


/// <summary>
///     Draws from the MENU table and leaves the cursor sitting after "select> " with
///     no newline, so the terminal stays the menu's until a key arrives. The FPGA
///     line is sampled at draw time, which is why redrawing is how it is refreshed.
/// </summary>
static void print_menu( void )
{
    mark_write();
    BSP_ConsolePrintf( "\n=== Forgix menu ===   up %lus   FPGA %s\n\n",
                       (unsigned long) uptime_seconds(),
                       BSP_FpgaIsReady() ? "ready" : "UNAVAILABLE" );
    for ( size_t index = 0; index < sizeof MENU / sizeof MENU[ 0 ]; ++index )
    {
        mark_write();
        BSP_ConsolePrintf( "  %c  %-22s %s\n", MENU[ index ].key, MENU[ index ].label,
                           MENU[ index ].detail );
    }
    mark_write();
    BSP_ConsolePrintf( "\nselect> " );
}


/* Steps are offered as 1..9 then a..e, because a single keypress is the whole
   input method and fourteen of them will not fit in the digits. */
/// <summary>
///     Maps a step index onto the single key that selects it. Nothing bounds the
///     index: one past the table yields the next letter, which select_step then
///     fails to match, so an over-long list would quietly lose its tail rather than
///     dispatch the wrong step.
/// </summary>
/// <returns>
///     The key character offered for this step.
/// </returns>
static char step_key( uint32_t index )
{
    return index < 9u ? (char) ( '1' + index ) : (char) ( 'a' + ( index - 9u ) );
}


/// <summary>
///     Lists the steps from the built-in test's own count, so this menu cannot
///     offer a step that does not exist, and leaves the same open prompt the main
///     menu does. The "x" line is why select_step reserves that key ahead of the
///     table.
/// </summary>
static void print_steps( void )
{
    mark_write();
    BSP_ConsolePrintf( "\n=== One test at a time ===\n\n" );
    for ( uint32_t index = 0; index < application_ibit_step_count(); ++index )
    {
        mark_write();
        BSP_ConsolePrintf( "  %c  %s\n", step_key( index ), application_ibit_step_name( index ) );
    }
    mark_write();
    BSP_ConsolePrintf( "  x  back to the menu\n\nselect> " );
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
    ui.mode = UI_MODE_MENU;
    print_menu();
}


/* An activity owns the LED for its whole run, not just the parts that paint it.
   The heartbeat is a 2 Hz writer and every activity here holds a colour for
   longer than that, so sharing the LED means the heartbeat showing through the
   middle of whatever the activity was trying to display. */
/// <summary>
///     Hands over the LED and the terminal in one move, then runs the activity's
///     start() inline: anything it prints appears before this returns, and the first
///     poll() does not come until the next pass of the loop.
/// </summary>
static void start_activity( const application_activity_t *activity )
{
    application_diagnostics_release_led();
    ui.mode = UI_MODE_ACTIVITY;
    ui.activity = activity;
    activity->start();
}


/// <summary>
///     The mirror of start_activity -- drops the activity, takes the LED back and
///     redraws. It deliberately does not call stop(): an activity that ended of its
///     own accord has already tidied up, and the abort path calls stop() before
///     reaching here, so calling it would run cleanup twice.
/// </summary>
static void finish_activity( void )
{
    ui.activity = NULL;
    application_diagnostics_reclaim_led();
    enter_menu();
}


/// <summary>
///     Reprints the menu once an activity's output has scrolled it away. Routed
///     through enter_menu rather than print_menu so a redraw is exactly the same
///     operation as arriving at the menu, with no second path to keep in step.
/// </summary>
static void action_redraw( void )
{
    enter_menu();
}


/// <summary>
///     Starts the full built-in test. From here the UI holds it as it holds any
///     other activity, so the any-key abort and the LED handover behave exactly as
///     they do for the blinkers.
/// </summary>
static void action_ibit( void )
{
    start_activity( application_ibit_sequence() );
}


/// <summary>
///     Starts the repeating built-in test. It is the one activity whose poll never
///     returns false, so the abort branch is the only path by which this entry is
///     ever left.
/// </summary>
static void action_soak( void )
{
    start_activity( application_ibit_soak() );
}


/// <summary>
///     The one entry that starts nothing. It switches the UI into a second menu
///     whose keys are steps, which is why the poll routes UI_MODE_STEPS to
///     select_step instead of select_entry.
/// </summary>
static void action_steps( void )
{
    ui.mode = UI_MODE_STEPS;
    print_steps();
}


/// <summary>
///     Prints inside the caller's own pass and redraws immediately, so the report is
///     not an activity: there is no window during which a keypress could abort it,
///     and the watchdog is not fed until it finishes.
/// </summary>
static void action_report( void )
{
    application_ibit_print_board_report();
    enter_menu();
}


/// <summary>
///     Starts the plain blinker. It is held as an activity purely so that the LED
///     handover and the any-key abort apply to it, not because it has any result to
///     report.
/// </summary>
static void action_blinker( void )
{
    start_activity( application_effects_blinker() );
}


/// <summary>
///     Starts the effects activity that works through heartbeat, colour wheel and
///     aurora. It paints the LED continuously for its whole run, which is why the
///     diagnostics heartbeat is released rather than left to compete with it.
/// </summary>
static void action_advanced( void )
{
    start_activity( application_effects_advanced() );
}


/// <summary>
///     Hands the terminal to the command shell, the one mode that is not an
///     activity: no stop(), no LED handover, and it keeps the terminal until the
///     shell's own `menu` command hands it back through application_ui_enter_menu.
/// </summary>
static void action_shell( void )
{
    ui.mode = UI_MODE_SHELL;
    application_console_start();
}


/// <summary>
///     Prints before rebooting because the call does not return. Without the line
///     the serial port would simply disappear, which is indistinguishable from a
///     crash at the far end.
/// </summary>
static void action_reboot( void )
{
    mark_write();
    BSP_ConsolePrintf( "rebooting\n" );
    BSP_McuReboot();
}


/// <summary>
///     Warns before handing the board to the USB loader, because that call does not
///     return either and the board comes back as a mass-storage drive rather than a
///     serial port. Only reflashing brings this firmware back.
/// </summary>
static void action_bootsel( void )
{
    mark_write();
    BSP_ConsolePrintf( "entering BOOTSEL; the serial port will disappear\n" );
    BSP_McuRebootToBootsel();
}


/* An unrecognized key redraws rather than complaining. The menu is the only
   thing on screen that says which keys exist, so showing it again is both the
   error message and the fix. */
/// <summary>
///     Scans MENU in order, so a duplicated key would be resolved by table position
///     and by nothing else. The value is narrowed to a char here, which is only safe
///     because the caller has already filtered out the timeout sentinel.
/// </summary>
static void select_entry( int16_t character )
{
    for ( size_t index = 0; index < sizeof MENU / sizeof MENU[ 0 ]; ++index )
    {
        if ( MENU[ index ].key == (char) character )
        {
            MENU[ index ].action();
            return;
        }
    }
    enter_menu();
}


/// <summary>
///     Checks "x" ahead of the table, so no step key can ever shadow the way back,
///     and reprints the step list rather than the main menu on an unknown key -- a
///     stray keypress should not throw the user out of the submenu they chose.
/// </summary>
static void select_step( int16_t character )
{
    if ( (char) character == 'x' )
    {
        enter_menu();
        return;
    }
    for ( uint32_t index = 0; index < application_ibit_step_count(); ++index )
    {
        if ( step_key( index ) == (char) character )
        {
            start_activity( application_ibit_single( index ) );
            return;
        }
    }
    print_steps();
}
