/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_ui_menu.h"

#include <stddef.h>
#include <stdint.h>

#include "application_console.h"
#include "application_effects.h"
#include "application_ibit.h"
#include "application_ui.h"
#include "application_ui_internal.h"
#include "bsp.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


typedef struct
{
    char key;
    const char *label;
    const char *detail;
    void ( *action )( void );
} menu_entry_t;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static char step_key( uint32_t index );

static void print_steps( void );

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

/* Takes the address of the ten action_* handlers above; C requires a
   function be declared before its address is taken, so this table follows
   the prototypes it binds instead of sitting under Private Variable
   Declarations with the rest of the module's data. One table drives both
   the rendering and the dispatch, so a key can never be offered without
   doing something or do something without being offered. */
static const menu_entry_t MENU[] = {
    { '1', "Built-in test", "the whole sequence, once", action_ibit },
    { '2', "Built-in test soak", "repeat with a tally until a key is pressed", action_soak },
    { '3', "One test at a time", "re-run a single step without the other fourteen", action_steps },
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
///     Draws from the MENU table and leaves the cursor sitting after "select> " with
///     no newline, so the terminal stays the menu's until a key arrives. The FPGA
///     line is sampled at draw time, which is why redrawing is how it is refreshed.
/// </summary>
void application_ui_menu_print( void )
{
    application_ui_mark_write();
    BSP_ConsolePrintf( "\n=== Forgix menu ===   up %lus   FPGA %s\n\n",
                       (unsigned long) application_ui_uptime_seconds(),
                       BSP_FpgaIsReady() ? "ready" : "UNAVAILABLE" );
    for ( size_t index = 0; index < sizeof MENU / sizeof MENU[ 0 ]; ++index )
    {
        application_ui_mark_write();
        BSP_ConsolePrintf( "  %c  %-22s %s\n", MENU[ index ].key, MENU[ index ].label,
                           MENU[ index ].detail );
    }
    application_ui_mark_write();
    BSP_ConsolePrintf( "\nselect> " );
}


/* An unrecognized key redraws rather than complaining. The menu is the only
   thing on screen that says which keys exist, so showing it again is both the
   error message and the fix. */
/// <summary>
///     Scans MENU in order, so a duplicated key would be resolved by table position
///     and by nothing else. The value is narrowed to a char here, which is only safe
///     because the caller has already filtered out the timeout sentinel.
/// </summary>
void application_ui_menu_select_entry( int16_t character )
{
    for ( size_t index = 0; index < sizeof MENU / sizeof MENU[ 0 ]; ++index )
    {
        if ( MENU[ index ].key == (char) character )
        {
            MENU[ index ].action();
            return;
        }
    }
    application_ui_enter_menu();
}


/// <summary>
///     Checks "x" ahead of the table, so no step key can ever shadow the way back,
///     and reprints the step list rather than the main menu on an unknown key -- a
///     stray keypress should not throw the user out of the submenu they chose.
/// </summary>
void application_ui_menu_select_step( int16_t character )
{
    if ( (char) character == 'x' )
    {
        application_ui_enter_menu();
        return;
    }
    for ( uint32_t index = 0; index < application_ibit_step_count(); ++index )
    {
        if ( step_key( index ) == (char) character )
        {
            application_ui_start_activity( application_ibit_single( index ) );
            return;
        }
    }
    print_steps();
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/* Steps are offered as 1..9 then a..f, because a single keypress is the whole
   input method and fifteen of them will not fit in the digits. */
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
    application_ui_mark_write();
    BSP_ConsolePrintf( "\n=== One test at a time ===\n\n" );
    for ( uint32_t index = 0; index < application_ibit_step_count(); ++index )
    {
        application_ui_mark_write();
        BSP_ConsolePrintf( "  %c  %s\n", step_key( index ), application_ibit_step_name( index ) );
    }
    application_ui_mark_write();
    BSP_ConsolePrintf( "  x  back to the menu\n\nselect> " );
}


/// <summary>
///     Reprints the menu once an activity's output has scrolled it away. Routed
///     through enter_menu rather than print_menu so a redraw is exactly the same
///     operation as arriving at the menu, with no second path to keep in step.
/// </summary>
static void action_redraw( void )
{
    application_ui_enter_menu();
}


/// <summary>
///     Starts the full built-in test. From here the UI holds it as it holds any
///     other activity, so the any-key abort and the LED handover behave exactly as
///     they do for the blinkers.
/// </summary>
static void action_ibit( void )
{
    application_ui_start_activity( application_ibit_sequence() );
}


/// <summary>
///     Starts the repeating built-in test. It is the one activity whose poll never
///     returns false, so the abort branch is the only path by which this entry is
///     ever left.
/// </summary>
static void action_soak( void )
{
    application_ui_start_activity( application_ibit_soak() );
}


/// <summary>
///     The one entry that starts nothing. It switches the UI into a second menu
///     whose keys are steps, which is why the poll routes APPLICATION_UI_MODE_STEPS
///     to select_step instead of select_entry.
/// </summary>
static void action_steps( void )
{
    ui.mode = APPLICATION_UI_MODE_STEPS;
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
    application_ui_enter_menu();
}


/// <summary>
///     Starts the plain blinker. It is held as an activity purely so that the LED
///     handover and the any-key abort apply to it, not because it has any result to
///     report.
/// </summary>
static void action_blinker( void )
{
    application_ui_start_activity( application_effects_blinker() );
}


/// <summary>
///     Starts the effects activity that works through heartbeat, colour wheel and
///     aurora. It paints the LED continuously for its whole run, which is why the
///     diagnostics heartbeat is released rather than left to compete with it.
/// </summary>
static void action_advanced( void )
{
    application_ui_start_activity( application_effects_advanced() );
}


/// <summary>
///     Hands the terminal to the command shell, the one mode that is not an
///     activity: no stop(), no LED handover, and it keeps the terminal until the
///     shell's own `menu` command hands it back through application_ui_enter_menu.
/// </summary>
static void action_shell( void )
{
    ui.mode = APPLICATION_UI_MODE_SHELL;
    application_console_start();
}


/// <summary>
///     Prints before rebooting because the call does not return. Without the line
///     the serial port would simply disappear, which is indistinguishable from a
///     crash at the far end.
/// </summary>
static void action_reboot( void )
{
    application_ui_mark_write();
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
    application_ui_mark_write();
    BSP_ConsolePrintf( "entering BOOTSEL; the serial port will disappear\n" );
    BSP_McuRebootToBootsel();
}
