/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "application_diagnostics.h"
#include "application_effects.h"
#include "mock_bsp_console.h"
#include "mock_bsp_time.h"
#include "mock_bsp_usb.h"
#include "mock_bsp_watchdog.h"
#include "mock_auto_bsp_button.h"
#include "mock_auto_bsp_fpga.h"
#include "mock_auto_bsp_led.h"




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bsp_led_state_t led_state( uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness );

static const application_activity_t *start_at( const application_activity_t *activity,
                                               uint32_t now_ms );




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


void test_blinker_cycles_red_green_and_blue_with_a_gap_between_each( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );
    BSP_LedGet_ExpectAndReturn( led_state( 1, 2, 3, 4 ) );
    BSP_LedSet_Expect( 255, 0, 0, 160 );
    BSP_LedSet_Expect( 0, 0, 0, 160 );
    BSP_LedSet_Expect( 0, 255, 0, 160 );
    BSP_LedSet_Expect( 0, 0, 0, 160 );
    BSP_LedSet_Expect( 0, 0, 255, 160 );
    BSP_LedSet_Expect( 0, 0, 0, 160 );
    /* Seventh update wraps to red again: the blinker never ends on its own. */
    BSP_LedSet_Expect( 255, 0, 0, 160 );

    const application_activity_t *activity = start_at( application_effects_blinker(), 1000 );
    for ( uint32_t step = 0; step < 7u; ++step )
    {
        MOCK_BSP_TimeSetMs( 1000 + step * 500u );
        TEST_ASSERT_TRUE( activity->poll() );
        /* A pass between updates must not drive the LED, or the blink rate would
           be the loop rate rather than 1 Hz. */
        MOCK_BSP_TimeSetMs( 1000 + step * 500u + 100u );
        TEST_ASSERT_TRUE( activity->poll() );
    }

    BSP_LedSet_Expect( 1, 2, 3, 4 );
    activity->stop();
}


void test_blinker_says_why_it_cannot_run_without_the_fpga( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( false );

    const application_activity_t *activity = start_at( application_effects_blinker(), 1000 );

    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(), "the LED is behind the FPGA, which is not responding" ) );
    TEST_ASSERT_FALSE( activity->poll() );

    /* Nothing was saved, so stopping must not write a colour back. */
    activity->stop();
}


/* The whole show, sampled densely enough to walk every sector of the wheel and
   every leg of the aurora blend. */
void test_advanced_blinker_runs_the_whole_show_and_then_restores_the_colour( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );
    BSP_LedGet_ExpectAndReturn( led_state( 7, 7, 7, 7 ) );
    BSP_LedSet_Ignore();

    const application_activity_t *activity = start_at( application_effects_advanced(), 1000 );

    for ( uint32_t elapsed = 0; elapsed < 18000u; elapsed += 50u )
    {
        MOCK_BSP_TimeSetMs( 1000 + elapsed );
        TEST_ASSERT_TRUE( activity->poll() );
    }

    BSP_LedSet_Expect( 7, 7, 7, 7 );
    MOCK_BSP_TimeSetMs( 1000 + 18000u );
    TEST_ASSERT_FALSE( activity->poll() );

    /* Already restored on the way out, so a later stop is a no-op. */
    activity->stop();
}


void test_advanced_blinker_paints_each_phase_of_the_show( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );
    BSP_LedGet_ExpectAndReturn( led_state( 7, 7, 7, 7 ) );

    const application_activity_t *activity = start_at( application_effects_advanced(), 0 );

    /* Heartbeat: red, with the pulse envelope on the brightness. */
    BSP_LedSet_Expect( 255, 24, 24, 16 );
    TEST_ASSERT_TRUE( activity->poll() );

    /* A pass between updates drives nothing. The foreground loop runs about a
       thousand times a second and the show is painted twenty times a second, so
       most passes have to be cheap or the FPGA bus never gets a rest. */
    MOCK_BSP_TimeSetMs( 10 );
    TEST_ASSERT_TRUE( activity->poll() );

    /* Colour wheel, at the very start of its first sector: full red. */
    BSP_LedSet_Expect( 255, 0, 0, 160 );
    MOCK_BSP_TimeSetMs( 4000 );
    TEST_ASSERT_TRUE( activity->poll() );

    /* Aurora, at its first stop. */
    BSP_LedSet_Expect( 0, 40, 80, 160 );
    MOCK_BSP_TimeSetMs( 10000 );
    TEST_ASSERT_TRUE( activity->poll() );

    BSP_LedSet_Expect( 7, 7, 7, 7 );
    activity->stop();
}


void test_advanced_blinker_stops_immediately_without_the_fpga( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( false );

    const application_activity_t *activity = start_at( application_effects_advanced(), 1000 );

    TEST_ASSERT_FALSE( activity->poll() );
    activity->stop();
}


void test_effects_mark_their_own_path_for_the_watchdog( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );
    BSP_LedGet_ExpectAndReturn( led_state( 1, 2, 3, 4 ) );
    BSP_LedSet_Ignore();

    const application_activity_t *activity = start_at( application_effects_blinker(), 1000 );
    TEST_ASSERT_TRUE( activity->poll() );

    TEST_ASSERT_TRUE( MOCK_BSP_WatchdogMarkerWasWritten( APPLICATION_DIAGNOSTICS_MARKER_EFFECT ) );
    activity->stop();
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


static bsp_led_state_t led_state( uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness )
{
    bsp_led_state_t led = {
        .red = red, .green = green, .blue = blue, .brightness = brightness, .enabled = true };
    return led;
}


static const application_activity_t *start_at( const application_activity_t *activity,
                                               uint32_t now_ms )
{
    MOCK_BSP_TimeSetMs( now_ms );
    activity->start();
    return activity;
}
