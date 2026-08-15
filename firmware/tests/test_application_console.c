/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "unity.h"

#include <stdint.h>
#include <string.h>

#include "application.h"
#include "application_console.h"
#include "application_console_status.h"
#include "application_diagnostics.h"
#include "application_diagnostics_led.h"
#include "application_diagnostics_report.h"
#include "application_time.h"
#include "mock_bsp_console.h"
#include "mock_bsp_time.h"
#include "mock_bsp_usb.h"
#include "mock_bsp_watchdog.h"
#include "mock_auto_application_memtest.h"
#include "mock_auto_application_ui.h"
#include "mock_auto_bsp_button.h"
#include "mock_auto_bsp_fpga.h"
#include "mock_auto_bsp_led.h"
#include "mock_auto_bsp_memory.h"




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static void start_at( uint32_t now_ms );

/* Stands in for the read the UI layer now owns, so every test below still reads
   as "one pass of the foreground loop" without the console doing its own read. */
static void poll_at( uint32_t now_ms );

static void poll_text_at( const char *text, uint32_t now_ms );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


void setUp( void )
{
    MOCK_BSP_ConsoleReset();
    MOCK_BSP_TimeReset();
    MOCK_BSP_UsbReset();
    MOCK_BSP_WatchdogReset();
}


void tearDown( void )
{
}


void test_console_echoes_a_command_and_coalesces_crlf( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();

    poll_text_at( "help\r\n", 100 );

    TEST_ASSERT_EQUAL_STRING( "help\r\n"
                              "hello | color <r> <g> <b> [brightness] | off | status | diag | "
                              "memid | memtest | menu | reset | "
                              "echo <on|off> | watch <seconds|off> | quiet | interactive | help\n"
                              "forgix> ",
                              MOCK_BSP_ConsoleOutput() );
}


void test_console_accepts_lf_and_empty_lines( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();

    poll_text_at( "\n", 100 );

    TEST_ASSERT_EQUAL_STRING( "\r\nforgix> ", MOCK_BSP_ConsoleOutput() );
}


void test_backspace_delete_and_ctrl_u_edit_the_local_line( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();

    MOCK_BSP_ConsoleQueueCharacter( 'a' );
    MOCK_BSP_ConsoleQueueCharacter( '\b' );
    MOCK_BSP_ConsoleQueueCharacter( '\b' );
    MOCK_BSP_ConsoleQueueCharacter( 'b' );
    MOCK_BSP_ConsoleQueueCharacter( 127 );
    MOCK_BSP_ConsoleQueueText( "cd" );
    MOCK_BSP_ConsoleQueueCharacter( 21 );
    for ( uint32_t index = 0; index < 8u; ++index )
    {
        poll_at( 100 );
    }

    TEST_ASSERT_EQUAL_STRING( "a\b \b\ab\b \bcd\b \b\b \b", MOCK_BSP_ConsoleOutput() );
}


void test_ctrl_c_cancels_input_and_ctrl_l_redraws_it( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();

    poll_text_at( "xy", 100 );
    MOCK_BSP_ConsoleQueueCharacter( 12 );
    poll_at( 100 );
    MOCK_BSP_ConsoleQueueCharacter( 3 );
    poll_at( 100 );

    TEST_ASSERT_EQUAL_STRING( "xy\r\nforgix> xy^C\r\nforgix> ", MOCK_BSP_ConsoleOutput() );
}


void test_nonprinting_input_is_ignored_and_overflow_rings_the_bell( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();

    MOCK_BSP_ConsoleQueueCharacter( 1 );
    poll_at( 100 );
    for ( uint32_t index = 0; index < 128u; ++index )
    {
        MOCK_BSP_ConsoleQueueCharacter( 'x' );
        poll_at( 100 );
    }

    TEST_ASSERT_EQUAL_UINT32( 128, strlen( MOCK_BSP_ConsoleOutput() ) );
    TEST_ASSERT_EQUAL_CHAR( '\a', MOCK_BSP_ConsoleOutput()[ 127 ] );
}


void test_quiet_mode_keeps_machine_commands_free_of_echo_prompts_and_telemetry( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();
    poll_text_at( "quiet\r", 100 );

    TEST_ASSERT_EQUAL_STRING( "quiet\r\nok\n", MOCK_BSP_ConsoleOutput() );
    MOCK_BSP_ConsoleReset();
    poll_at( 50000 );
    MOCK_BSP_ConsoleQueueCharacter( 'x' );
    MOCK_BSP_ConsoleQueueCharacter( '\b' );
    poll_at( 50001 );
    poll_at( 50002 );
    poll_text_at( "help\r", 50100 );

    TEST_ASSERT_EQUAL_STRING( "hello | color <r> <g> <b> [brightness] | off | status | diag | "
                              "memid | memtest | menu | reset | "
                              "echo <on|off> | watch <seconds|off> | quiet | interactive | help\n",
                              MOCK_BSP_ConsoleOutput() );
}


void test_interactive_mode_restores_echo_prompt_and_idle_reporting( void )
{
    start_at( 0 );
    poll_text_at( "quiet\r", 100 );
    MOCK_BSP_ConsoleReset();

    poll_text_at( "interactive\r", 200 );
    MOCK_BSP_ConsoleQueueCharacter( 'x' );
    poll_at( 201 );

    TEST_ASSERT_EQUAL_STRING( "ok\nforgix> x", MOCK_BSP_ConsoleOutput() );
}


void test_echo_can_be_disabled_and_reenabled_without_changing_command_responses( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();
    poll_text_at( "echo off\r", 100 );
    MOCK_BSP_ConsoleReset();

    MOCK_BSP_ConsoleQueueCharacter( 'x' );
    MOCK_BSP_ConsoleQueueCharacter( '\b' );
    poll_at( 150 );
    poll_at( 151 );
    poll_text_at( "help\r", 200 );
    TEST_ASSERT_EQUAL_STRING(
        "hello | color <r> <g> <b> [brightness] | off | status | diag | memid | memtest | menu | "
        "reset | "
        "echo <on|off> | watch <seconds|off> | quiet | interactive | help\nforgix> ",
        MOCK_BSP_ConsoleOutput() );

    MOCK_BSP_ConsoleReset();
    poll_text_at( "echo on\r", 300 );
    MOCK_BSP_ConsoleQueueCharacter( 'x' );
    poll_at( 301 );
    TEST_ASSERT_EQUAL_STRING( "ok\nforgix> x", MOCK_BSP_ConsoleOutput() );
}


/* The read marker belongs to the UI layer now, since that is what performs the
   read; this covers the two paths the console still owns. */
void test_console_marks_the_write_and_command_paths_for_the_watchdog( void )
{
    start_at( 0 );
    TEST_ASSERT_EQUAL_UINT32( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE,
                              MOCK_BSP_WatchdogMarker() );

    poll_text_at( "help\r", 100 );
    TEST_ASSERT_TRUE( MOCK_BSP_WatchdogMarkerWasWritten( APPLICATION_DIAGNOSTICS_MARKER_COMMAND ) );
}


/* The `menu` command runs inside command dispatch, so the shell finishes the line
   it was given after the menu has already been drawn. Without releasing it, that
   prints one last `forgix> ` under the menu and the screen claims two different
   things about which prompt is live. */
void test_released_console_stops_prompting_and_stops_scheduling_status( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();

    application_console_release();
    poll_text_at( "help\r", 100 );

    TEST_ASSERT_EQUAL_STRING( "help\r\n"
                              "hello | color <r> <g> <b> [brightness] | off | status | diag | "
                              "memid | memtest | menu | reset | "
                              "echo <on|off> | watch <seconds|off> | quiet | interactive | help\n",
                              MOCK_BSP_ConsoleOutput() );

    MOCK_BSP_ConsoleReset();
    poll_at( 60000 );
    TEST_ASSERT_EQUAL_STRING( "", MOCK_BSP_ConsoleOutput() );
}


void test_ctrl_c_and_ctrl_l_remain_silent_in_quiet_mode( void )
{
    start_at( 0 );
    poll_text_at( "quiet\r", 100 );
    MOCK_BSP_ConsoleReset();

    MOCK_BSP_ConsoleQueueCharacter( 12 );
    MOCK_BSP_ConsoleQueueCharacter( 3 );
    poll_at( 200 );
    poll_at( 200 );

    TEST_ASSERT_EQUAL_STRING( "", MOCK_BSP_ConsoleOutput() );
}


/* The ^C marker and the repaint mirror what was typed, so with echo off they
   have nothing to mirror: a script that happens to send 0x03 or 0x0C must not
   get terminal-rendering bytes injected into its stream. The prompt after a
   cancel is command-class output and still prints, matching a completed line. */
void test_ctrl_c_and_ctrl_l_are_silent_with_echo_off( void )
{
    start_at( 0 );
    poll_text_at( "echo off\r", 100 );
    MOCK_BSP_ConsoleReset();

    MOCK_BSP_ConsoleQueueCharacter( 12 );
    poll_at( 200 );
    TEST_ASSERT_EQUAL_STRING( "", MOCK_BSP_ConsoleOutput() );

    MOCK_BSP_ConsoleQueueCharacter( 3 );
    poll_at( 200 );
    TEST_ASSERT_EQUAL_STRING( "forgix> ", MOCK_BSP_ConsoleOutput() );
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


static void start_at( uint32_t now_ms )
{
    MOCK_BSP_TimeSetMs( now_ms );
    application_console_start();
}


static void poll_at( uint32_t now_ms )
{
    MOCK_BSP_TimeSetMs( now_ms );
    int16_t character = BSP_ConsoleGetCharTimeoutUs( 1000 );
    if ( character >= 0 )
    {
        application_console_feed( character );
    }
    else
    {
        application_console_idle();
    }
}


static void poll_text_at( const char *text, uint32_t now_ms )
{
    MOCK_BSP_ConsoleQueueText( text );
    for ( uint32_t index = 0; index < (uint32_t) strlen( text ); ++index )
    {
        poll_at( now_ms );
    }
}
