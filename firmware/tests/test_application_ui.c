/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "unity.h"

#include <stdint.h>
#include <string.h>

#include "application_diagnostics.h"
#include "application_ui.h"
#include "application_ui_menu.h"
#include "application_time.h"
#include "mock_bsp_console.h"
#include "mock_bsp_time.h"
#include "mock_bsp_usb.h"
#include "mock_bsp_watchdog.h"
#include "mock_auto_application_console.h"
#include "mock_auto_application_diagnostics.h"
#include "mock_auto_application_effects.h"
#include "mock_auto_application_ibit.h"
#include "mock_auto_application_memtest.h"
#include "mock_auto_bsp_button.h"
#include "mock_auto_bsp_fpga.h"
#include "mock_auto_bsp_led.h"
#include "mock_auto_bsp_mcu.h"
#include "mock_auto_bsp_memory.h"




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* A stand-in for whatever the menu starts, so the UI tests exercise the activity
   contract -- start, repeated poll, abort through stop -- without dragging the
   built-in test's own behavior in with it. */
static uint32_t activity_starts;
static uint32_t activity_polls;
static uint32_t activity_stops;
static uint32_t activity_polls_before_finishing;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static void fake_start( void );

static bool fake_poll( void );

static void fake_stop( void );

static void start_at( uint32_t now_ms );

static void poll_at( uint32_t now_ms );

static void key_at( char key, uint32_t now_ms );

static void open_menu_at( uint32_t now_ms );

/* Must follow the declarations above: the initializer takes the address of
   fake_start, fake_poll and fake_stop, which have to be declared first. */
static const application_activity_t FAKE_ACTIVITY = {
    .name = "fake",
    .start = fake_start,
    .poll = fake_poll,
    .stop = fake_stop,
};




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
    activity_starts = 0;
    activity_polls = 0;
    activity_stops = 0;
    activity_polls_before_finishing = 3;
    application_console_release_Ignore();
    application_ibit_step_count_IgnoreAndReturn( 15 );
    application_ibit_step_name_IgnoreAndReturn( "a step" );
}


void tearDown( void )
{
}


void test_banner_repeats_once_a_second_until_a_key_arrives( void )
{
    start_at( 0 );

    poll_at( 0 );
    poll_at( 999 );
    poll_at( 1000 );

    TEST_ASSERT_EQUAL_STRING( "hello world - 1 - press any key\n"
                              "hello world - 2 - press any key\n",
                              MOCK_BSP_ConsoleOutput() );
}


/* The count is what tells a user how long the board has been up by the time they
   found the port, so it must advance while nobody is listening. */
void test_banner_counts_while_the_host_is_absent_so_it_reads_as_uptime( void )
{
    MOCK_BSP_UsbSetConnected( false );
    start_at( 0 );

    poll_at( 0 );
    poll_at( 1000 );
    TEST_ASSERT_EQUAL_STRING( "", MOCK_BSP_ConsoleOutput() );

    MOCK_BSP_UsbSetConnected( true );
    poll_at( 2000 );
    TEST_ASSERT_EQUAL_STRING( "hello world - 3 - press any key\n", MOCK_BSP_ConsoleOutput() );
}


/* 'r' is the reboot key. Reaching for "any key" must not be able to fire it. */
void test_the_key_that_ends_the_banner_does_not_also_select_from_the_menu( void )
{
    start_at( 0 );

    BSP_FpgaIsReady_ExpectAndReturn( true );
    key_at( 'r', 0 );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "=== Forgix menu ===" ) );
    TEST_ASSERT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "rebooting" ) );
}


/* Real bytes are 0..255 and the timeout sentinel is only the common negative:
   an SDK error code other than -1 must read as "nothing arrived", not as a
   keystroke. Feeding it to the console would fail here as an unexpected call. */
void test_a_negative_console_error_is_not_a_shell_keystroke( void )
{
    open_menu_at( 0 );
    application_console_start_Expect();
    key_at( 'c', 100 );

    MOCK_BSP_ConsoleQueueResult( -2 );
    application_console_idle_Expect();
    poll_at( 200 );
}


/* The same phantom keystroke in activity mode would abort a running soak --
   an hours-long tally ended by a USB hiccup rather than by the user. */
void test_a_negative_console_error_does_not_abort_an_activity( void )
{
    open_menu_at( 0 );
    application_diagnostics_release_led_Expect();
    application_ibit_soak_ExpectAndReturn( &FAKE_ACTIVITY );
    key_at( '2', 100 );
    MOCK_BSP_ConsoleReset();

    MOCK_BSP_ConsoleQueueResult( -2 );
    poll_at( 200 );

    TEST_ASSERT_EQUAL_UINT32( 1, activity_polls );
    TEST_ASSERT_EQUAL_UINT32( 0, activity_stops );
    TEST_ASSERT_EQUAL_STRING( "", MOCK_BSP_ConsoleOutput() );
}


void test_menu_command_takes_the_terminal_back_from_the_shell( void )
{
    open_menu_at( 0 );
    application_console_start_Expect();
    key_at( 'c', 100 );
    MOCK_BSP_ConsoleReset();

    BSP_FpgaIsReady_ExpectAndReturn( true );
    application_ui_enter_menu();
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "=== Forgix menu ===" ) );

    /* Back under the menu, a key selects again rather than reaching the shell. */
    BSP_FpgaIsReady_ExpectAndReturn( true );
    key_at( '?', 200 );
}


void test_enter_activity_takes_the_terminal_and_finishes_into_the_menu( void )
{
    open_menu_at( 0 );
    application_console_start_Expect();
    key_at( 'c', 100 );
    MOCK_BSP_ConsoleReset();

    application_diagnostics_release_led_Expect();
    application_ui_enter_activity( &FAKE_ACTIVITY );
    TEST_ASSERT_EQUAL_UINT32( 1, activity_starts );

    poll_at( 200 );
    poll_at( 300 );
    application_diagnostics_reclaim_led_Expect();
    BSP_FpgaIsReady_ExpectAndReturn( true );
    poll_at( 400 );

    TEST_ASSERT_EQUAL_UINT32( 3, activity_polls );
    TEST_ASSERT_EQUAL_UINT32( 0, activity_stops );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "=== Forgix menu ===" ) );
}


/* Nothing is due in the menu, so an empty poll must stay silent -- otherwise the
   menu would scroll itself off the screen while the user is reading it. */
void test_menu_stays_silent_while_it_waits( void )
{
    open_menu_at( 0 );

    poll_at( 60000 );

    TEST_ASSERT_EQUAL_STRING( "", MOCK_BSP_ConsoleOutput() );
}


void test_built_in_test_runs_as_an_activity_and_returns_to_the_menu( void )
{
    open_menu_at( 0 );

    application_diagnostics_release_led_Expect();
    application_ibit_sequence_ExpectAndReturn( &FAKE_ACTIVITY );
    key_at( '1', 100 );
    TEST_ASSERT_EQUAL_UINT32( 1, activity_starts );

    poll_at( 200 );
    poll_at( 300 );
    TEST_ASSERT_EQUAL_UINT32( 2, activity_polls );

    /* The third poll finishes it, and the menu comes back on its own. */
    application_diagnostics_reclaim_led_Expect();
    BSP_FpgaIsReady_ExpectAndReturn( true );
    poll_at( 400 );
    TEST_ASSERT_EQUAL_UINT32( 0, activity_stops );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "=== Forgix menu ===" ) );
}


/* Any key aborts, because a user watching a test they no longer want should not
   have to remember which key means stop. */
void test_any_key_aborts_a_running_activity_and_lets_it_clean_up( void )
{
    open_menu_at( 0 );
    application_diagnostics_release_led_Expect();
    application_ibit_soak_ExpectAndReturn( &FAKE_ACTIVITY );
    key_at( '2', 100 );
    MOCK_BSP_ConsoleReset();

    application_diagnostics_reclaim_led_Expect();
    BSP_FpgaIsReady_ExpectAndReturn( true );
    key_at( 'q', 200 );

    TEST_ASSERT_EQUAL_UINT32( 1, activity_stops );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "aborted" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "=== Forgix menu ===" ) );
}


void test_ui_marks_the_menu_and_read_paths_for_the_watchdog( void )
{
    start_at( 0 );

    poll_at( 0 );

    TEST_ASSERT_TRUE(
        MOCK_BSP_WatchdogMarkerWasWritten( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_READ ) );
    TEST_ASSERT_TRUE(
        MOCK_BSP_WatchdogMarkerWasWritten( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE ) );
    TEST_ASSERT_TRUE( MOCK_BSP_WatchdogMarkerWasWritten( APPLICATION_DIAGNOSTICS_MARKER_MENU ) );
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


static void fake_start( void )
{
    ++activity_starts;
}


static bool fake_poll( void )
{
    ++activity_polls;
    return activity_polls < activity_polls_before_finishing;
}


static void fake_stop( void )
{
    ++activity_stops;
}


static void start_at( uint32_t now_ms )
{
    MOCK_BSP_TimeSetMs( now_ms );
    application_ui_start();
}


static void poll_at( uint32_t now_ms )
{
    MOCK_BSP_TimeSetMs( now_ms );
    application_ui_poll();
}


static void key_at( char key, uint32_t now_ms )
{
    MOCK_BSP_ConsoleQueueCharacter( (uint8_t) key );
    poll_at( now_ms );
}


/* Drives a fresh boot and the banner-dismissing keypress, so the tests that care
   about menu behavior neither repeat the way in nor inherit the mode a previous
   test left in the module's static state. */
static void open_menu_at( uint32_t now_ms )
{
    start_at( now_ms );
    application_console_release_Ignore();
    BSP_FpgaIsReady_ExpectAndReturn( true );
    key_at( ' ', now_ms );
    MOCK_BSP_ConsoleReset();
}
