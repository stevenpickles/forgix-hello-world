/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "application_ibit.h"
#include "application_ibit_steps_board.h"
#include "application_ibit_steps_fpga.h"
#include "application_time.h"
#include "mock_bsp_console.h"
#include "mock_bsp_time.h"
#include "mock_bsp_usb.h"
#include "mock_auto_application_diagnostics.h"
#include "mock_bsp_watchdog.h"
#include "mock_auto_bsp_adc.h"
#include "mock_auto_bsp_button.h"
#include "mock_auto_bsp_clocks.h"
#include "mock_auto_bsp_fpga.h"
#include "mock_auto_bsp_led.h"
#include "mock_auto_bsp_mcu.h"
#include "mock_auto_bsp_memory.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


enum
{
    STEP_FPGA_CONFIGURATION = 10,
    STEP_FPGA_REGISTERS = 11,
    STEP_LED = 12,
    STEP_BUTTON = 13,
    STEP_FPGA_CLOCK = 14,
};




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bsp_led_state_t led_state( uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness );

static const char *run_step_at( uint32_t index, uint32_t now_ms );

static const char *run_step( uint32_t index );

static void run_clock_step_with( uint32_t first_tick, uint32_t second_tick );

static void expect_led_phase( uint8_t red, uint8_t green, uint8_t blue );




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




/***** the FPGA and what sits behind it *****/


void test_fpga_configuration_passes_and_implies_the_oscillator( void )
{
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_FpgaStatusPin_ExpectAndReturn( true );

    const char *output = run_step( STEP_FPGA_CONFIGURATION );

    TEST_ASSERT_NOT_NULL( strstr( output, "PASS" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "cdone=1 id=B8" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "32MHz oscillator implied" ) );
}


void test_fpga_configuration_fails_on_a_wrong_design_id( void )
{
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( 0x00u );
    BSP_FpgaStatusPin_ExpectAndReturn( false );

    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_FPGA_CONFIGURATION ), "FAIL" ) );
}


/* A walking pattern, because 0x00 and 0xFF are what a bus stuck low or high
   returns and either would pass a test that wrote them. */
void test_fpga_register_bus_round_trips_a_walking_pattern_and_restores_the_colour( void )
{
    const bsp_led_state_t saved = led_state( 1, 2, 3, 4 );

    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_FpgaReadStatus_ExpectAndReturn( 0x01u );
    BSP_LedGet_ExpectAndReturn( saved );
    BSP_LedSet_Expect( 0x5au, 0xa5u, 0x3cu, 0xc3u );
    BSP_LedGet_ExpectAndReturn( led_state( 0x5au, 0xa5u, 0x3cu, 0xc3u ) );
    BSP_LedRestore_Expect( &saved );

    const char *output = run_step( STEP_FPGA_REGISTERS );

    TEST_ASSERT_NOT_NULL( strstr( output, "PASS" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "wrote 5A,A5,3C,C3 read 5A,A5,3C,C3" ) );
}


/* The register-bus step runs against whatever the user had showing, including
   an LED they had turned off -- and its walking pattern lights the LED, so the
   restore is the only thing standing between the step and leaving it lit. */
void test_the_register_bus_step_restores_an_led_the_user_had_off( void )
{
    bsp_led_state_t saved = led_state( 1, 2, 3, 4 );
    saved.enabled = false;

    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_FpgaReadStatus_ExpectAndReturn( 0x01u );
    BSP_LedGet_ExpectAndReturn( saved );
    BSP_LedSet_Expect( 0x5au, 0xa5u, 0x3cu, 0xc3u );
    BSP_LedGet_ExpectAndReturn( led_state( 0x5au, 0xa5u, 0x3cu, 0xc3u ) );
    BSP_LedRestore_Expect( &saved );

    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_FPGA_REGISTERS ), "PASS" ) );
}


/* One wrong byte at a time. A bus can corrupt a single register, and a test that
   only ever sees all four wrong together would pass three of those faults. */
void test_fpga_register_bus_fails_when_any_single_register_misreads( void )
{
    const bsp_led_state_t corrupted[] = {
        led_state( 0x00u, 0xa5u, 0x3cu, 0xc3u ),
        led_state( 0x5au, 0x00u, 0x3cu, 0xc3u ),
        led_state( 0x5au, 0xa5u, 0x00u, 0xc3u ),
        led_state( 0x5au, 0xa5u, 0x3cu, 0x00u ),
    };

    const bsp_led_state_t saved = led_state( 1, 2, 3, 4 );

    for ( uint32_t index = 0; index < 4u; ++index )
    {
        BSP_FpgaCdone_ExpectAndReturn( true );
        BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
        BSP_FpgaReadStatus_ExpectAndReturn( 0x01u );
        BSP_LedGet_ExpectAndReturn( saved );
        BSP_LedSet_Expect( 0x5au, 0xa5u, 0x3cu, 0xc3u );
        BSP_LedGet_ExpectAndReturn( corrupted[ index ] );
        BSP_LedRestore_Expect( &saved );

        TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_FPGA_REGISTERS ), "FAIL" ) );
    }
}


/* The skip decision is this run's evidence, not what bring-up recorded. An FPGA
   that answers CDONE but returns the wrong design ID is not one the LED and
   button tests can say anything about either. */
void test_a_wrong_design_id_also_stands_the_dependent_steps_down( void )
{
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( 0x00u );

    const char *output = run_step( STEP_LED );

    TEST_ASSERT_NOT_NULL( strstr( output, "SKIP" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "FPGA not responding; this test sits behind it" ) );
}


/* Skipped rather than failed: the LED and the button are both behind the FPGA,
   so a verdict on either would describe the bus and not the part being named. */
void test_steps_behind_the_fpga_are_skipped_when_it_is_unreachable( void )
{
    const uint32_t behind[] = { STEP_FPGA_REGISTERS, STEP_LED, STEP_BUTTON, STEP_FPGA_CLOCK };

    for ( uint32_t index = 0; index < 4u; ++index )
    {
        BSP_FpgaCdone_ExpectAndReturn( false );
        const char *output = run_step( behind[ index ] );
        TEST_ASSERT_NOT_NULL( strstr( output, "SKIP" ) );
        TEST_ASSERT_NOT_NULL( strstr( output, "FPGA not responding; this test sits behind it" ) );
    }
}


void test_led_drives_each_channel_in_turn_and_restores_the_previous_colour( void )
{
    const bsp_led_state_t saved = led_state( 9, 8, 7, 6 );

    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_LedGet_ExpectAndReturn( saved );
    expect_led_phase( 255, 0, 0 );
    expect_led_phase( 0, 255, 0 );
    expect_led_phase( 0, 0, 255 );
    expect_led_phase( 255, 255, 255 );
    expect_led_phase( 0, 0, 0 );
    BSP_LedRestore_Expect( &saved );

    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_single( STEP_LED );
    activity->start();
    MOCK_BSP_ConsoleReset();

    /* A pass with no time elapsed must not advance the phase, or the whole show
       would flash past in one iteration of the foreground loop. */
    TEST_ASSERT_TRUE( activity->poll() );
    TEST_ASSERT_TRUE( activity->poll() );
    for ( uint32_t phase = 1; phase < 5u; ++phase )
    {
        MOCK_BSP_TimeSetMs( 1000 + phase * 200u );
        TEST_ASSERT_TRUE( activity->poll() );
    }
    MOCK_BSP_TimeSetMs( 2000 );
    TEST_ASSERT_FALSE( activity->poll() );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "PASS" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "previous colour restored" ) );
}


/* Each channel checked on its own, so a single dead colour is caught rather than
   only the case where all three fail together. */
void test_led_fails_and_restores_the_colour_when_any_channel_does_not_read_back( void )
{
    const bsp_led_state_t corrupted[] = {
        led_state( 0, 0, 0, 128 ),
        led_state( 255, 99, 0, 128 ),
        led_state( 255, 0, 99, 128 ),
    };

    const bsp_led_state_t saved = led_state( 9, 8, 7, 6 );

    for ( uint32_t index = 0; index < 3u; ++index )
    {
        BSP_FpgaCdone_ExpectAndReturn( true );
        BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
        BSP_LedGet_ExpectAndReturn( saved );
        BSP_LedSet_Expect( 255, 0, 0, 128 );
        BSP_LedGet_ExpectAndReturn( corrupted[ index ] );
        BSP_LedRestore_Expect( &saved );

        const char *output = run_step( STEP_LED );

        TEST_ASSERT_NOT_NULL( strstr( output, "FAIL" ) );
        TEST_ASSERT_NOT_NULL( strstr( output, "readback mismatch at step 0" ) );
    }
}


/* The count is the FPGA's own debounced edge count, and it is what decides. */
void test_button_passes_when_the_count_moves_and_the_level_is_seen( void )
{
    bsp_button_state_t before = { .level = 1, .count = 0 };
    bsp_button_state_t pressed = { .level = 0, .count = 1 };
    bsp_button_state_t holding = { .level = 0, .count = 0 };

    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_ButtonClearCount_Expect();
    BSP_ButtonGetState_ExpectAndReturn( before );
    BSP_ButtonGetState_ExpectAndReturn( holding );
    BSP_ButtonGetState_ExpectAndReturn( pressed );

    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_single( STEP_BUTTON );
    activity->start();
    MOCK_BSP_ConsoleReset();

    TEST_ASSERT_TRUE( activity->poll() );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "press SW1 within 15s" ) );

    /* Held down across two samples, so the second one finds the level already
       known to have moved and does not need to look again. */
    TEST_ASSERT_TRUE( activity->poll() );
    MOCK_BSP_TimeSetMs( 3400 );
    TEST_ASSERT_FALSE( activity->poll() );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "PASS" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(),
                                  "pressed after 2.4s, count 0 -> 1, level seen to move" ) );
}


/* The regression: a tap shorter than the 50 ms poll interval increments the
   FPGA's counter and is back at rest before the level is next read. That is a
   working button, and gating on the level reported it as a timeout. */
void test_button_passes_on_a_tap_too_brief_for_the_level_to_be_sampled( void )
{
    bsp_button_state_t before = { .level = 1, .count = 0 };
    bsp_button_state_t after = { .level = 1, .count = 1 };

    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_ButtonClearCount_Expect();
    BSP_ButtonGetState_ExpectAndReturn( before );
    BSP_ButtonGetState_ExpectAndReturn( after );

    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_single( STEP_BUTTON );
    activity->start();
    MOCK_BSP_ConsoleReset();

    TEST_ASSERT_TRUE( activity->poll() );
    MOCK_BSP_TimeSetMs( 1100 );
    TEST_ASSERT_FALSE( activity->poll() );

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL( strstr( output, "PASS" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "level never sampled moving" ) );
}


/* An unattended run has to finish, so silence is a timeout and not a failure. */
void test_button_times_out_without_failing_when_nobody_presses_it( void )
{
    bsp_button_state_t idle = { .level = 1, .count = 4 };

    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_ButtonClearCount_Expect();
    BSP_ButtonGetState_ExpectAndReturn( idle );
    BSP_ButtonGetState_ExpectAndReturn( idle );
    BSP_ButtonGetState_ExpectAndReturn( idle );

    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_single( STEP_BUTTON );
    activity->start();
    MOCK_BSP_ConsoleReset();

    TEST_ASSERT_TRUE( activity->poll() );
    TEST_ASSERT_TRUE( activity->poll() );
    /* Between poll intervals there is nothing to ask the FPGA, which is what
       keeps a fifteen-second wait from being fifteen thousand bus transactions. */
    MOCK_BSP_TimeSetMs( 1020 );
    TEST_ASSERT_TRUE( activity->poll() );
    MOCK_BSP_TimeSetMs( 1000 + APPLICATION_IBIT_BUTTON_TIMEOUT_MS );
    TEST_ASSERT_FALSE( activity->poll() );

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL( strstr( output, "TIMEOUT" ) );
    TEST_ASSERT_NULL( strstr( output, "FAIL" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "no press within 15s" ) );
}


/* The step judges FPGA ticks against MCU milliseconds -- a ratio -- so the mock
   feeds it exact numbers: sixteen million ticks over half a second is 32 MHz on
   the nose. The mid-window poll is the branch where there is nothing to ask the
   FPGA and the step just waits. */
void test_fpga_clock_passes_when_the_counter_tracks_32mhz( void )
{
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_FpgaTickSample_ExpectAndReturn( 1000u );
    BSP_FpgaTickSample_ExpectAndReturn( 1000u + 16000000u );

    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_single( STEP_FPGA_CLOCK );
    activity->start();
    MOCK_BSP_ConsoleReset();

    TEST_ASSERT_TRUE( activity->poll() );
    MOCK_BSP_TimeSetMs( 1400 );
    TEST_ASSERT_TRUE( activity->poll() );
    MOCK_BSP_TimeSetMs( 1500 );
    TEST_ASSERT_FALSE( activity->poll() );

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL( strstr( output, "PASS" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "fpga=32.000MHz over 500ms, 16000000 ticks" ) );
}


/* Slow and fast are separate tests because within_tolerance takes the
   difference in whichever order keeps it unsigned, and a suite that only ever
   ran slow would leave the other order untried. */
void test_fpga_clock_fails_when_the_counter_runs_slow( void )
{
    run_clock_step_with( 5000u, 5000u + 15000000u );

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL( strstr( output, "FAIL" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "fpga=30.000MHz" ) );
}


void test_fpga_clock_fails_when_the_counter_runs_fast( void )
{
    run_clock_step_with( 5000u, 5000u + 16400000u );

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL( strstr( output, "FAIL" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "fpga=32.800MHz" ) );
}


/* The counter wraps every 134 seconds and no capture is entitled to a wrap-free
   window; unsigned subtraction is the whole wrap handling and this pins it. */
void test_fpga_clock_measures_across_a_counter_wrap( void )
{
    run_clock_step_with( 0xfff00000u, (uint32_t) ( 0xfff00000u + 16000000u ) );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "PASS" ) );
}


/* Exactly one percent out still passes: the band is inclusive, the same
   contract the MCU clock step gets from within_tolerance's <=. */
void test_fpga_clock_passes_on_the_tolerance_boundary( void )
{
    run_clock_step_with( 0u, 16160000u );

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL( strstr( output, "PASS" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "fpga=32.320MHz" ) );
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


/* Runs one step in isolation and returns everything it printed. Each step is
   driven through application_ibit_single so the runner, the tally and the result
   line are exercised alongside the step itself. */
static const char *run_step_at( uint32_t index, uint32_t now_ms )
{
    MOCK_BSP_TimeSetMs( now_ms );
    const application_activity_t *activity = application_ibit_single( index );
    activity->start();
    MOCK_BSP_ConsoleReset();
    while ( activity->poll() )
    {
        MOCK_BSP_TimeSetMs( now_ms );
    }
    return MOCK_BSP_ConsoleOutput();
}


static const char *run_step( uint32_t index )
{
    return run_step_at( index, 1000 );
}


/* Both tick samples are declared up front and the window is crossed in one
   jump: the verdict arithmetic is the subject here, and the mid-window waiting
   already has its own test. */
static void run_clock_step_with( uint32_t first_tick, uint32_t second_tick )
{
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_FpgaTickSample_ExpectAndReturn( first_tick );
    BSP_FpgaTickSample_ExpectAndReturn( second_tick );

    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_single( STEP_FPGA_CLOCK );
    activity->start();
    MOCK_BSP_ConsoleReset();

    TEST_ASSERT_TRUE( activity->poll() );
    MOCK_BSP_TimeSetMs( 1500 );
    TEST_ASSERT_FALSE( activity->poll() );
}


static void expect_led_phase( uint8_t red, uint8_t green, uint8_t blue )
{
    BSP_LedSet_Expect( red, green, blue, 128 );
    BSP_LedGet_ExpectAndReturn( led_state( red, green, blue, 128 ) );
}
