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

static void expect_ready_status( uint8_t count );




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


/* The shell is entered from the menu, by which point the user has already proved
   the link works, so it opens straight into the idle cadence rather than the
   once-a-second reporting the banner now does instead. */
void test_console_starts_with_a_prompt_and_reports_idle_status_after_the_timeout( void )
{
    start_at( 500 );
    TEST_ASSERT_EQUAL_STRING( "forgix> ", MOCK_BSP_ConsoleOutput() );

    poll_at( 10499 );
    expect_ready_status( 7 );
    poll_at( 10500 );
    expect_ready_status( 8 );
    poll_at( 20500 );

    TEST_ASSERT_EQUAL_STRING(
        "forgix> \r\nid=B8 status=01 button=03 count=7 fpga_status=1\n"
        "forgix> \r\nid=B8 status=01 button=03 count=8 fpga_status=1\nforgix> ",
        MOCK_BSP_ConsoleOutput() );
}


void test_received_character_wins_over_a_due_status_and_protects_partial_input( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();

    MOCK_BSP_ConsoleQueueCharacter( 'h' );
    poll_at( APPLICATION_IDLE_TIMEOUT_MS );
    poll_at( APPLICATION_IDLE_TIMEOUT_MS * 2u );

    TEST_ASSERT_EQUAL_STRING( "h", MOCK_BSP_ConsoleOutput() );
}


void test_completed_command_resumes_periodic_status_after_the_idle_timeout( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();
    poll_text_at( "help\r", 100 );
    MOCK_BSP_ConsoleReset();

    poll_at( 10099 );
    expect_ready_status( 9 );
    poll_at( 10100 );
    expect_ready_status( 10 );
    poll_at( 20100 );

    TEST_ASSERT_EQUAL_STRING(
        "\r\nid=B8 status=01 button=03 count=9 fpga_status=1\n"
        "forgix> \r\nid=B8 status=01 button=03 count=10 fpga_status=1\nforgix> ",
        MOCK_BSP_ConsoleOutput() );
}


void test_watch_uses_the_requested_period_and_stops_before_echoing_a_key( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();
    poll_text_at( "watch 2\r", 100 );
    MOCK_BSP_ConsoleReset();

    poll_at( 2099 );
    expect_ready_status( 11 );
    poll_at( 2100 );
    MOCK_BSP_ConsoleQueueCharacter( 'h' );
    poll_at( 4100 );
    poll_at( 20000 );

    TEST_ASSERT_EQUAL_STRING( "\r\nid=B8 status=01 button=03 count=11 fpga_status=1\nforgix> h",
                              MOCK_BSP_ConsoleOutput() );
}


/* The regression this pins: any keystroke used to stop the watch outright, so
   the completed line fell back to the 10 s idle cadence and an hourly watch
   silently became a ten-second one -- or a two-second one silently a ten-second
   one. A completed command must hand the watch back with its period intact. */
void test_watch_survives_a_completed_command_line( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();
    poll_text_at( "watch 2\r", 100 );
    expect_ready_status( 14 );
    poll_at( 2100 );
    poll_text_at( "help\r", 2200 );
    MOCK_BSP_ConsoleReset();

    poll_at( 4199 );
    expect_ready_status( 15 );
    poll_at( 4200 );

    TEST_ASSERT_EQUAL_STRING( "\r\nid=B8 status=01 button=03 count=15 fpga_status=1\nforgix> ",
                              MOCK_BSP_ConsoleOutput() );
}


/* `quiet` then `watch` used to clear the quiet flag but leave echo off -- a
   half-quiet state no command could name, escapable only through
   `interactive`. Arming a watch is an interactive act, so echo, prompt, and
   telemetry all come back with it. */
void test_watch_after_quiet_restores_echo_and_the_prompt( void )
{
    start_at( 0 );
    poll_text_at( "quiet\r", 100 );
    MOCK_BSP_ConsoleReset();

    poll_text_at( "watch 2\r", 200 );
    MOCK_BSP_ConsoleQueueCharacter( 'x' );
    poll_at( 300 );

    TEST_ASSERT_EQUAL_STRING( "ok\nforgix> x", MOCK_BSP_ConsoleOutput() );
}


void test_an_empty_line_leaves_a_watch_running( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();
    poll_text_at( "watch 2\r", 100 );
    poll_text_at( "\r", 300 );
    MOCK_BSP_ConsoleReset();

    poll_at( 2299 );
    expect_ready_status( 16 );
    poll_at( 2300 );

    TEST_ASSERT_EQUAL_STRING( "\r\nid=B8 status=01 button=03 count=16 fpga_status=1\nforgix> ",
                              MOCK_BSP_ConsoleOutput() );
}


/* Cancelling is the one line ending that ends a watch: the ^C is aimed at
   whatever is currently claiming the terminal, and a watch that survived it
   would keep claiming it every period. */
void test_ctrl_c_ends_a_running_watch_and_falls_back_to_idle_status( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();
    poll_text_at( "watch 2\r", 100 );
    MOCK_BSP_ConsoleQueueCharacter( 3 );
    poll_at( 300 );
    MOCK_BSP_ConsoleReset();

    poll_at( 2300 );
    TEST_ASSERT_EQUAL_STRING( "", MOCK_BSP_ConsoleOutput() );

    expect_ready_status( 17 );
    poll_at( 10300 );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "count=17" ) );
}


void test_watch_off_suppresses_idle_status_until_interactive_mode_is_restored( void )
{
    start_at( 0 );
    MOCK_BSP_ConsoleReset();
    poll_text_at( "watch off\r", 100 );
    MOCK_BSP_ConsoleReset();

    poll_at( 50000 );
    TEST_ASSERT_EQUAL_STRING( "", MOCK_BSP_ConsoleOutput() );

    poll_text_at( "interactive\r", 50100 );
    MOCK_BSP_ConsoleReset();
    expect_ready_status( 12 );
    poll_at( 60100 );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "count=12" ) );
}


void test_unsolicited_status_is_withheld_until_the_host_asserts_dtr( void )
{
    MOCK_BSP_UsbSetConnected( false );
    start_at( 500 );
    MOCK_BSP_ConsoleReset();

    poll_at( 10500 );
    poll_at( 11500 );
    TEST_ASSERT_EQUAL_STRING( "", MOCK_BSP_ConsoleOutput() );

    MOCK_BSP_UsbSetConnected( true );
    expect_ready_status( 13 );
    poll_at( 12500 );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "count=13" ) );
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


static void expect_ready_status( uint8_t count )
{
    bsp_button_state_t button = { .level = 0x03, .count = count };
    BSP_FpgaIsReady_ExpectAndReturn( true );
    BSP_ButtonGetState_ExpectAndReturn( button );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_FpgaReadStatus_ExpectAndReturn( 0x01 );
    BSP_FpgaStatusPin_ExpectAndReturn( true );
}
