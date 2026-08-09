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
    STEP_CHIP_IDENTITY = 0,
    STEP_BOARD_IDENTITY = 1,
    STEP_CLOCKS = 2,
    STEP_MEMORY_SIZING = 3,
    STEP_OTP_DEVINFO = 4,
    STEP_BOOT_FLASH = 5,
    STEP_PSRAM = 6,
    STEP_TEMPERATURE = 7,
    STEP_USB = 8,
    STEP_WATCHDOG = 9,
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


static bsp_mcu_info_t healthy_mcu( void );

static bsp_clocks_report_t healthy_clocks( void );

static bsp_memory_report_t healthy_memory( void );

static bsp_memory_psram_identity_t healthy_identity( void );

static bsp_memory_sweep_result_t ok_sweep( void );

static bsp_usb_health_t usb_health( uint32_t frame, bool connected, bool suspended,
                                    uint32_t write_available );

static bsp_adc_temperature_t temperature_sample( uint16_t raw, int32_t milli_celsius );

static bsp_led_state_t led_state( uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness );

static uint32_t tick_sample_callback( int num_calls );

static void expect_led_phase( uint8_t red, uint8_t green, uint8_t blue );

static void expect_sequence_without_the_fpga( void );

static void drive_to_completion( const application_activity_t *activity, uint32_t from_ms,
                                 uint32_t to_ms );

static bsp_led_state_t led_get_callback( int num_calls );

static bsp_button_state_t button_callback( int num_calls );

static void ignore_a_healthy_board( void );

static bsp_button_state_t button_never_pressed_callback( int num_calls );




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




/***** the sequence, the soak and the report *****/


/* A board with a dead FPGA and a frozen start-of-frame counter: two real faults,
   three tests that cannot mean anything without the FPGA, and one measurement
   with no pass criterion. The point is that all four land in different columns
   rather than collapsing into one number. */
void test_the_sequence_runs_every_step_and_summarises_the_tally( void )
{
    application_diagnostics_boot_reason_IgnoreAndReturn( BSP_BOOT_POWER_ON );
    MOCK_BSP_UsbSetHealth( usb_health( 100, true, false, 256 ) );
    expect_sequence_without_the_fpga();

    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_sequence();
    activity->start();
    drive_to_completion( activity, 1000, 60000 );

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL( strstr( output, "Initiated built-in test" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "[ 1/15] Chip identity" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "[15/15] FPGA 32MHz clock" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "IBIT: 8 PASS  2 FAIL  0 TIMEOUT  4 SKIP  1 INFO" ) );
}


/* The soak never finishes on its own; it is stopped by the abort path, which is
   also what has to put the LED back. */
void test_the_soak_tallies_across_iterations_and_starts_the_next_run( void )
{
    application_diagnostics_boot_reason_IgnoreAndReturn( BSP_BOOT_POWER_ON );
    MOCK_BSP_UsbSetHealth( usb_health( 100, true, false, 256 ) );
    expect_sequence_without_the_fpga();

    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_soak();
    activity->start();

    uint32_t now_ms = 1000;
    while ( strstr( MOCK_BSP_ConsoleOutput(), "soak: 1 run(s)" ) == NULL )
    {
        TEST_ASSERT_TRUE( activity->poll() );
        now_ms += 100u;
        MOCK_BSP_TimeSetMs( now_ms > 60000u ? 60000u : now_ms );
    }

    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(), "soak: 1 run(s), 1 with a failure, 0 with a timeout" ) );
    activity->stop();
}


void test_aborting_after_the_led_step_puts_the_previous_colour_back( void )
{
    const bsp_led_state_t saved = led_state( 9, 8, 7, 6 );

    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_LedGet_ExpectAndReturn( saved );
    expect_led_phase( 255, 0, 0 );

    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_single( STEP_LED );
    activity->start();
    TEST_ASSERT_TRUE( activity->poll() );

    BSP_LedRestore_Expect( &saved );
    activity->stop();

    /* A second stop must not write again; there is nothing left saved. */
    activity->stop();
}


/* The abort path with the LED saved dark: the whole state comes back, including
   the enable bit the walking colours forced on. */
void test_aborting_the_led_step_puts_back_an_led_the_user_had_off( void )
{
    bsp_led_state_t saved = led_state( 9, 8, 7, 6 );
    saved.enabled = false;

    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_LedGet_ExpectAndReturn( saved );
    expect_led_phase( 255, 0, 0 );

    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_single( STEP_LED );
    activity->start();
    TEST_ASSERT_TRUE( activity->poll() );

    BSP_LedRestore_Expect( &saved );
    activity->stop();
}


/* The soak counts iterations that had a failure, not iterations, so a clean run
   has to leave the failure tally alone. */
void test_a_clean_soak_iteration_does_not_count_as_a_failure( void )
{
    ignore_a_healthy_board();

    uint32_t frame = 100;
    MOCK_BSP_UsbSetHealth( usb_health( frame, true, false, 256 ) );
    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_soak();
    activity->start();

    uint32_t now_ms = 1000;
    while ( strstr( MOCK_BSP_ConsoleOutput(), "soak: 1 run(s)" ) == NULL )
    {
        TEST_ASSERT_TRUE( activity->poll() );
        now_ms += 100u;
        MOCK_BSP_TimeSetMs( now_ms );
        MOCK_BSP_UsbSetHealth( usb_health( ++frame, true, false, 256 ) );
    }

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL( strstr( output, "IBIT: 14 PASS  0 FAIL  0 TIMEOUT  0 SKIP  1 INFO" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "soak: 1 run(s), 0 with a failure, 0 with a timeout" ) );
    activity->stop();
}


/* The case a real burn-in is: nobody is at the bench, so the button times out
   every iteration. That must not read as a failing run, or the one number a soak
   exists to produce equals the run count forever and says nothing. */
void test_an_unattended_soak_iteration_counts_a_timeout_and_not_a_failure( void )
{
    ignore_a_healthy_board();
    BSP_ButtonGetState_StubWithCallback( button_never_pressed_callback );

    uint32_t frame = 100;
    MOCK_BSP_UsbSetHealth( usb_health( frame, true, false, 256 ) );
    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_soak();
    activity->start();

    uint32_t now_ms = 1000;
    while ( strstr( MOCK_BSP_ConsoleOutput(), "soak: 1 run(s)" ) == NULL )
    {
        TEST_ASSERT_TRUE( activity->poll() );
        now_ms += 100u;
        MOCK_BSP_TimeSetMs( now_ms );
        MOCK_BSP_UsbSetHealth( usb_health( ++frame, true, false, 256 ) );
    }

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL( strstr( output, "13 PASS  0 FAIL  1 TIMEOUT" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "soak: 1 run(s), 0 with a failure, 1 with a timeout" ) );
    activity->stop();
}


void test_step_names_are_published_for_the_menu( void )
{
    TEST_ASSERT_EQUAL_UINT32( 15, application_ibit_step_count() );
    TEST_ASSERT_EQUAL_STRING( "Chip identity", application_ibit_step_name( STEP_CHIP_IDENTITY ) );
    TEST_ASSERT_EQUAL_STRING( "Button SW1", application_ibit_step_name( STEP_BUTTON ) );
    TEST_ASSERT_EQUAL_STRING( "FPGA 32MHz clock", application_ibit_step_name( STEP_FPGA_CLOCK ) );
}


void test_the_board_report_states_the_facts_without_judging_them( void )
{
    BSP_McuInfo_ExpectAndReturn( healthy_mcu() );
    BSP_ClocksReport_ExpectAndReturn( healthy_clocks() );
    BSP_AdcTemperature_ExpectAndReturn( temperature_sample( 800, 24500 ) );

    application_ibit_print_board_report();

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL( strstr( output, "manufacturer=493 part=0004" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "board    E66038B7135F212C" ) );
    TEST_ASSERT_NOT_NULL(
        strstr( output, "package  id=11223344 device=01020304AABBCCDD valid=1" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "otp_cs0=0x9" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "measured sys=150000000" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "24500 milli-degrees C" ) );
    TEST_ASSERT_NULL( strstr( output, "PASS" ) );
}


void test_the_board_report_names_a_risc_v_image( void )
{
    bsp_mcu_info_t info = healthy_mcu();
    info.architecture = BSP_MCU_ARCHITECTURE_RISCV;
    BSP_McuInfo_ExpectAndReturn( info );
    BSP_ClocksReport_ExpectAndReturn( healthy_clocks() );
    BSP_AdcTemperature_ExpectAndReturn( temperature_sample( 800, 24500 ) );

    application_ibit_print_board_report();

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "RISC-V" ) );
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


static bsp_mcu_info_t healthy_mcu( void )
{
    bsp_mcu_info_t info = {
        .manufacturer = BSP_MCU_MANUFACTURER_RASPBERRY_PI,
        .part = BSP_MCU_PART_RP2350,
        .revision = 2,
        .package_id = 0x11223344u,
        .device_id_low = 0xaabbccddu,
        .device_id_high = 0x01020304u,
        .chip_info_valid = true,
        .unique_id = { 0xe6, 0x60, 0x38, 0xb7, 0x13, 0x5f, 0x21, 0x2c },
        .sram_bytes = 520u * 1024u,
        .flash_bytes = 2u * 1024u * 1024u,
        .otp_cs0_size_code = 0x9u, /* what the board actually reports: 2 MByte */
        .otp_cs1_size_code = 0u,
        .core_count = 2,
        .architecture = BSP_MCU_ARCHITECTURE_ARM,
    };
    return info;
}


static bsp_clocks_report_t healthy_clocks( void )
{
    bsp_clocks_report_t clocks = {
        .sys_hz = 150000000u,
        .usb_hz = 48000000u,
        .ref_hz = 12000000u,
        .peri_hz = 150000000u,
        .adc_hz = 48000000u,
        .measured_sys_hz = 150000000u,
        .measured_usb_hz = 48000000u,
    };
    return clocks;
}


static bsp_memory_report_t healthy_memory( void )
{
    bsp_memory_report_t memory = {
        .flash_bytes = 2u * 1024u * 1024u,
        .flash_ok = true,
        .psram_bytes = 2u * 1024u * 1024u,
        .psram_ok = true,
        .psram_forced = false,
        .psram_kgd = 0x0bu,
        .psram_eid = 0x43u,
        .psram_enabled = true,
    };
    return memory;
}


static bsp_usb_health_t usb_health( uint32_t frame, bool connected, bool suspended,
                                    uint32_t write_available )
{
    bsp_usb_health_t health = {
        .connected = connected,
        .suspended = suspended,
        .write_available = write_available,
        .activity_count = 1,
        .frame_number = frame,
    };
    return health;
}


static bsp_memory_psram_identity_t healthy_identity( void )
{
    bsp_memory_psram_identity_t identity = { .kgd = 0x0bu, .eid = 0x43u, .restored = true };
    return identity;
}


static bsp_memory_sweep_result_t ok_sweep( void )
{
    bsp_memory_sweep_result_t result = { .ok = true, .fail_address = 0 };
    return result;
}


/* A named helper rather than a compound literal at the call site: the commas in
   a designated initializer split CMock's expectation macros into extra
   arguments. */
static bsp_adc_temperature_t temperature_sample( uint16_t raw, int32_t milli_celsius )
{
    bsp_adc_temperature_t sample = { .raw = raw, .milli_celsius = milli_celsius };
    return sample;
}


static bsp_led_state_t led_state( uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness )
{
    bsp_led_state_t led = {
        .red = red, .green = green, .blue = blue, .brightness = brightness, .enabled = true };
    return led;
}


static void expect_led_phase( uint8_t red, uint8_t green, uint8_t blue )
{
    BSP_LedSet_Expect( red, green, blue, 128 );
    BSP_LedGet_ExpectAndReturn( led_state( red, green, blue, 128 ) );
}


/* Every step reachable without the FPGA, so the summary line and the tally are
   exercised across all five outcomes in one run. */
static void expect_sequence_without_the_fpga( void )
{
    BSP_McuInfo_ExpectAndReturn( healthy_mcu() );
    BSP_McuInfo_ExpectAndReturn( healthy_mcu() );
    BSP_ClocksReport_ExpectAndReturn( healthy_clocks() );
    BSP_McuInfo_ExpectAndReturn( healthy_mcu() );
    BSP_McuInfo_ExpectAndReturn( healthy_mcu() );
    BSP_MemoryCheck_ExpectAndReturn( healthy_memory() );
    BSP_MemoryPsramIdentify_ExpectAndReturn( healthy_identity() );
    BSP_MemoryPsramSweepChunk_IgnoreAndReturn( ok_sweep() );
    BSP_AdcTemperature_ExpectAndReturn( temperature_sample( 800, 24500 ) );
    BSP_FpgaCdone_ExpectAndReturn( false );
    BSP_FpgaPing_ExpectAndReturn( 0x00u );
    BSP_FpgaStatusPin_ExpectAndReturn( false );
    BSP_FpgaCdone_ExpectAndReturn( false );
    BSP_FpgaCdone_ExpectAndReturn( false );
    BSP_FpgaCdone_ExpectAndReturn( false );
    BSP_FpgaCdone_ExpectAndReturn( false );
}


static void drive_to_completion( const application_activity_t *activity, uint32_t from_ms,
                                 uint32_t to_ms )
{
    uint32_t now_ms = from_ms;
    while ( activity->poll() )
    {
        now_ms += 100u;
        MOCK_BSP_TimeSetMs( now_ms > to_ms ? to_ms : now_ms );
    }
}


/* Answers in the order one healthy sequence asks: the register-bus step reads
   the current colour and then its own pattern back, and the LED step reads the
   colour to restore followed by each of the five it drives. */
static bsp_led_state_t led_get_callback( int num_calls )
{
    switch ( num_calls )
    {
    case 0:
        return led_state( 9, 8, 7, 6 );
    case 1:
        return led_state( 0x5au, 0xa5u, 0x3cu, 0xc3u );
    case 2:
        return led_state( 9, 8, 7, 6 );
    case 3:
        return led_state( 255, 0, 0, 128 );
    case 4:
        return led_state( 0, 255, 0, 128 );
    case 5:
        return led_state( 0, 0, 255, 128 );
    case 6:
        return led_state( 255, 255, 255, 128 );
    default:
        return led_state( 0, 0, 0, 128 );
    }
}


static bsp_button_state_t button_callback( int num_calls )
{
    bsp_button_state_t before = { .level = 1, .count = 4 };
    bsp_button_state_t pressed = { .level = 0, .count = 5 };
    return num_calls == 0 ? before : pressed;
}


static void ignore_a_healthy_board( void )
{
    BSP_McuInfo_IgnoreAndReturn( healthy_mcu() );
    BSP_ClocksReport_IgnoreAndReturn( healthy_clocks() );
    BSP_MemoryCheck_IgnoreAndReturn( healthy_memory() );
    BSP_MemoryPsramIdentify_IgnoreAndReturn( healthy_identity() );
    BSP_MemoryPsramSweepChunk_IgnoreAndReturn( ok_sweep() );
    BSP_AdcTemperature_IgnoreAndReturn( temperature_sample( 800, 24500 ) );
    BSP_FpgaCdone_IgnoreAndReturn( true );
    BSP_FpgaPing_IgnoreAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_FpgaStatusPin_IgnoreAndReturn( true );
    BSP_FpgaReadStatus_IgnoreAndReturn( 0x01u );
    BSP_LedSet_Ignore();
    BSP_LedRestore_Ignore();
    BSP_LedGet_StubWithCallback( led_get_callback );
    BSP_ButtonClearCount_Ignore();
    BSP_ButtonGetState_StubWithCallback( button_callback );
    BSP_FpgaTickSample_StubWithCallback( tick_sample_callback );
    application_diagnostics_boot_reason_IgnoreAndReturn( BSP_BOOT_POWER_ON );
}


/* Consecutive samples differ by exactly sixteen million ticks: 32 MHz over the
   500 ms window the drive loops' 100 ms stride produces. A drive whose stride
   changes makes the healthy-board clock step fail loudly rather than drift
   silently out of tolerance. */
static uint32_t tick_sample_callback( int num_calls )
{
    return (uint32_t) num_calls * 16000000u;
}


static bsp_button_state_t button_never_pressed_callback( int num_calls )
{
    (void) num_calls;
    bsp_button_state_t idle = { .level = 1, .count = 4 };
    return idle;
}
