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


/* A stand-in for whatever the menu starts, so the dispatch tests exercise the
   activity contract -- start, repeated poll, abort through stop -- without
   dragging the built-in test's own behavior in with it. */
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


void test_menu_reports_uptime_and_a_healthy_fpga( void )
{
    start_at( 1000 );

    BSP_FpgaIsReady_ExpectAndReturn( true );
    key_at( ' ', 8000 );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "up 7s" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "FPGA ready" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "select> " ) );
}


/* A dead FPGA must be visible on the menu itself. The tests that diagnose it are
   reached from here, so the user has to know before they choose. */
void test_menu_names_an_unavailable_fpga( void )
{
    start_at( 0 );

    BSP_FpgaIsReady_ExpectAndReturn( false );
    key_at( ' ', 0 );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "FPGA UNAVAILABLE" ) );
}


/* The FPGA line is sampled at draw time, so a redraw after a runtime loss must
   show the loss -- a menu that kept saying "ready" from a boot-time answer is
   the bug this pins. */
void test_menu_redraw_reflects_a_readiness_change( void )
{
    start_at( 0 );

    BSP_FpgaIsReady_ExpectAndReturn( true );
    key_at( ' ', 0 );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "FPGA ready" ) );

    MOCK_BSP_ConsoleReset();
    BSP_FpgaIsReady_ExpectAndReturn( false );
    key_at( '?', 100 );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "FPGA UNAVAILABLE" ) );
}


void test_unknown_menu_key_redraws_rather_than_complaining( void )
{
    open_menu_at( 0 );

    BSP_FpgaIsReady_ExpectAndReturn( true );
    key_at( 'z', 100 );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "=== Forgix menu ===" ) );
}


void test_redraw_key_reprints_the_menu( void )
{
    open_menu_at( 0 );

    BSP_FpgaIsReady_ExpectAndReturn( true );
    key_at( '?', 100 );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "Redraw this menu" ) );
}


void test_shell_key_hands_the_terminal_to_the_console( void )
{
    open_menu_at( 0 );

    application_console_start_Expect();
    key_at( 'c', 100 );

    application_console_feed_Expect( 'x' );
    key_at( 'x', 200 );

    application_console_idle_Expect();
    poll_at( 300 );
}


void test_reboot_key_warns_before_restarting_the_board( void )
{
    open_menu_at( 0 );

    BSP_McuReboot_Expect();
    key_at( 'r', 100 );

    TEST_ASSERT_EQUAL_STRING( "rebooting\n", MOCK_BSP_ConsoleOutput() );
}


void test_bootsel_key_warns_that_the_port_is_about_to_vanish( void )
{
    open_menu_at( 0 );

    BSP_McuRebootToBootsel_Expect();
    key_at( 'b', 100 );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "serial port will disappear" ) );
}


void test_step_submenu_lists_every_step_and_runs_the_one_chosen( void )
{
    open_menu_at( 0 );

    key_at( '3', 100 );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "=== One test at a time ===" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "  1  a step" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "  x  back to the menu" ) );

    application_diagnostics_release_led_Expect();
    application_ibit_single_ExpectAndReturn( 2, &FAKE_ACTIVITY );
    key_at( '3', 200 );
    TEST_ASSERT_EQUAL_UINT32( 1, activity_starts );
}


/* Fifteen steps do not fit in the digits, so the tail of the list is lettered. */
void test_step_submenu_letters_the_steps_that_run_out_of_digits( void )
{
    open_menu_at( 0 );
    key_at( '3', 100 );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "  f  a step" ) );

    application_diagnostics_release_led_Expect();
    application_ibit_single_ExpectAndReturn( 9, &FAKE_ACTIVITY );
    key_at( 'a', 200 );
    TEST_ASSERT_EQUAL_UINT32( 1, activity_starts );
}


void test_step_submenu_redraws_an_unknown_key_and_leaves_on_x( void )
{
    open_menu_at( 0 );
    key_at( '3', 100 );
    MOCK_BSP_ConsoleReset();

    key_at( '#', 200 );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "=== One test at a time ===" ) );

    BSP_FpgaIsReady_ExpectAndReturn( true );
    key_at( 'x', 300 );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "=== Forgix menu ===" ) );
}


void test_blinker_and_advanced_blinker_start_from_the_menu( void )
{
    open_menu_at( 0 );

    application_diagnostics_release_led_Expect();
    application_effects_blinker_ExpectAndReturn( &FAKE_ACTIVITY );
    key_at( '5', 100 );
    TEST_ASSERT_EQUAL_UINT32( 1, activity_starts );

    application_diagnostics_reclaim_led_Expect();
    BSP_FpgaIsReady_ExpectAndReturn( true );
    key_at( 'q', 200 );

    application_diagnostics_release_led_Expect();
    application_effects_advanced_ExpectAndReturn( &FAKE_ACTIVITY );
    key_at( '6', 300 );
    TEST_ASSERT_EQUAL_UINT32( 2, activity_starts );
}


void test_memtest_starts_from_the_menu_with_the_activity_contract( void )
{
    open_menu_at( 0 );

    application_diagnostics_release_led_Expect();
    application_memtest_activity_ExpectAndReturn( &FAKE_ACTIVITY );
    key_at( '7', 100 );
    TEST_ASSERT_EQUAL_UINT32( 1, activity_starts );

    application_diagnostics_reclaim_led_Expect();
    BSP_FpgaIsReady_ExpectAndReturn( true );
    key_at( 'q', 200 );
    TEST_ASSERT_EQUAL_UINT32( 1, activity_stops );
}


void test_board_report_prints_and_comes_straight_back_to_the_menu( void )
{
    open_menu_at( 0 );

    application_ibit_print_board_report_Expect();
    BSP_FpgaIsReady_ExpectAndReturn( true );
    key_at( '4', 100 );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "=== Forgix menu ===" ) );
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
