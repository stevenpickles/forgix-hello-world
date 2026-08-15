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

static bsp_memory_sweep_result_t sweep_callback( bsp_memory_sweep_op op, uint32_t chunk_index,
                                                 int num_calls );

static bsp_usb_health_t usb_health( uint32_t frame, bool connected, bool suspended,
                                    uint32_t write_available );

static const char *run_step_at( uint32_t index, uint32_t now_ms );

static const char *run_step( uint32_t index );

static void run_usb_step_with( bool connected, bool suspended, uint32_t write_available,
                               uint32_t second_frame );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/* The sweep callback's shared state, reset per test: how many chunks each
   sweep pass should span, and which call -- if any -- stands in for a fault. */
static uint32_t sweep_chunks_expected;
static int sweep_fail_at_call;
static uint32_t sweep_fail_address;


void setUp( void )
{
    MOCK_BSP_ConsoleReset();
    MOCK_BSP_TimeReset();
    MOCK_BSP_UsbReset();
    MOCK_BSP_WatchdogReset();
    sweep_chunks_expected = 32;
    sweep_fail_at_call = -1;
    sweep_fail_address = 0;
}


void tearDown( void )
{
}




/***** identity, clocks and memory *****/


void test_chip_identity_passes_on_a_raspberry_pi_rp2350( void )
{
    BSP_McuInfo_ExpectAndReturn( healthy_mcu() );

    const char *output = run_step( STEP_CHIP_IDENTITY );

    TEST_ASSERT_NOT_NULL( strstr( output, "PASS" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "manufacturer=493 part=0004 revision=2 Arm x2" ) );
}


void test_chip_identity_fails_on_an_unexpected_part( void )
{
    bsp_mcu_info_t info = healthy_mcu();
    info.part = 0x0002u;
    info.architecture = BSP_MCU_ARCHITECTURE_RISCV;
    BSP_McuInfo_ExpectAndReturn( info );

    const char *output = run_step( STEP_CHIP_IDENTITY );

    TEST_ASSERT_NOT_NULL( strstr( output, "FAIL" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "RISC-V" ) );
}


void test_chip_identity_fails_on_an_unexpected_manufacturer( void )
{
    bsp_mcu_info_t info = healthy_mcu();
    info.manufacturer = 0x0123u;
    BSP_McuInfo_ExpectAndReturn( info );

    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_CHIP_IDENTITY ), "FAIL" ) );
}


void test_board_identity_reports_the_unique_id( void )
{
    BSP_McuInfo_ExpectAndReturn( healthy_mcu() );

    const char *output = run_step( STEP_BOARD_IDENTITY );

    TEST_ASSERT_NOT_NULL( strstr( output, "PASS" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "E66038B7135F212C" ) );
}


/* All-zero and all-ones are what a bus that answered with nothing looks like. */
void test_board_identity_rejects_an_all_zero_and_an_all_ones_id( void )
{
    bsp_mcu_info_t zeroed = healthy_mcu();
    memset( zeroed.unique_id, 0x00, sizeof zeroed.unique_id );
    BSP_McuInfo_ExpectAndReturn( zeroed );
    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_BOARD_IDENTITY ), "FAIL" ) );

    bsp_mcu_info_t ones = healthy_mcu();
    memset( ones.unique_id, 0xff, sizeof ones.unique_id );
    BSP_McuInfo_ExpectAndReturn( ones );
    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_BOARD_IDENTITY ), "FAIL" ) );
}


void test_clocks_pass_when_the_measured_frequencies_match( void )
{
    BSP_ClocksReport_ExpectAndReturn( healthy_clocks() );

    const char *output = run_step( STEP_CLOCKS );

    TEST_ASSERT_NOT_NULL( strstr( output, "PASS" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "sys=150.000MHz usb=48.000MHz" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "sys/usb=3.12x" ) );
}


/* The configured value still reads correct when a PLL never locked, so only the
   measured one can catch it. */
void test_clocks_fail_when_the_measurement_disagrees_with_the_configuration( void )
{
    bsp_clocks_report_t clocks = healthy_clocks();
    clocks.measured_sys_hz = 125000000u;
    BSP_ClocksReport_ExpectAndReturn( clocks );

    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_CLOCKS ), "FAIL" ) );

    clocks = healthy_clocks();
    clocks.measured_usb_hz = 12000000u;
    BSP_ClocksReport_ExpectAndReturn( clocks );

    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_CLOCKS ), "FAIL" ) );
}


/* Overspeed matters as much as underspeed: an over-locked PLL is still a PLL
   that is not doing what the SDK believes it is. */
void test_clocks_fail_when_the_measurement_runs_fast( void )
{
    bsp_clocks_report_t clocks = healthy_clocks();
    clocks.measured_sys_hz = 200000000u;
    BSP_ClocksReport_ExpectAndReturn( clocks );

    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_CLOCKS ), "FAIL" ) );
}


/* A clk_usb measured at zero is a dead domain, not a divide-by-zero. */
void test_clocks_report_a_zero_ratio_rather_than_dividing_by_a_dead_usb_clock( void )
{
    bsp_clocks_report_t clocks = healthy_clocks();
    clocks.measured_usb_hz = 0u;
    BSP_ClocksReport_ExpectAndReturn( clocks );

    const char *output = run_step( STEP_CLOCKS );

    TEST_ASSERT_NOT_NULL( strstr( output, "FAIL" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "sys/usb=0.00x" ) );
}


void test_memory_sizing_checks_flash_and_sram_against_what_the_part_should_have( void )
{
    BSP_McuInfo_ExpectAndReturn( healthy_mcu() );
    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_MEMORY_SIZING ), "flash=2048KiB sram=520KiB" ) );

    bsp_mcu_info_t info = healthy_mcu();
    info.flash_bytes = 16u * 1024u * 1024u;
    BSP_McuInfo_ExpectAndReturn( info );
    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_MEMORY_SIZING ), "FAIL" ) );

    info = healthy_mcu();
    info.sram_bytes = 264u * 1024u;
    BSP_McuInfo_ExpectAndReturn( info );
    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_MEMORY_SIZING ), "FAIL" ) );
}


/* Reported, never believed: these are unprogrammed OTP defaults on this part. */
void test_otp_device_info_is_reported_without_a_verdict( void )
{
    BSP_McuInfo_ExpectAndReturn( healthy_mcu() );

    const char *output = run_step( STEP_OTP_DEVINFO );

    TEST_ASSERT_NOT_NULL( strstr( output, "INFO" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "cs0=0x9 cs1=0x0" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "not used to size anything" ) );
}


void test_boot_flash_reports_the_reset_vector_verdict( void )
{
    BSP_MemoryCheck_ExpectAndReturn( healthy_memory() );
    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_BOOT_FLASH ), "reset vector sane" ) );

    bsp_memory_report_t memory = healthy_memory();
    memory.flash_ok = false;
    BSP_MemoryCheck_ExpectAndReturn( memory );
    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_BOOT_FLASH ), "FAIL" ) );
}


/* The fitted part answers KGD 0x0B rather than AP Memory's 0x5D. It works, so
   identity is a note on a passing result and not a failure. */
void test_psram_passes_a_working_device_while_naming_the_identity_mismatch( void )
{
    BSP_MemoryCheck_ExpectAndReturn( healthy_memory() );
    BSP_MemoryPsramIdentify_ExpectAndReturn( healthy_identity() );
    BSP_MemoryPsramSweepChunk_StubWithCallback( sweep_callback );

    const char *output = run_step( STEP_PSRAM );

    TEST_ASSERT_NOT_NULL( strstr( output, "PASS" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "2048KiB sweep held" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "kgd=0B eid=43" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "not the part on the schematic" ) );
}


/* The note is keyed on this run's legal-window bytes, not the boot capture:
   the report still says 0B here, and only the fresh read says 5D. */
void test_psram_stays_quiet_about_identity_when_the_expected_part_is_fitted( void )
{
    BSP_MemoryCheck_ExpectAndReturn( healthy_memory() );
    bsp_memory_psram_identity_t identity = healthy_identity();
    identity.kgd = 0x5du;
    identity.eid = 0x26u;
    BSP_MemoryPsramIdentify_ExpectAndReturn( identity );
    BSP_MemoryPsramSweepChunk_StubWithCallback( sweep_callback );

    const char *output = run_step( STEP_PSRAM );

    TEST_ASSERT_NOT_NULL( strstr( output, "kgd=5D eid=26" ) );
    TEST_ASSERT_NULL( strstr( output, "schematic" ) );
}


/* A build with the device compiled out is not a board with a broken one, and the
   report alone cannot tell them apart -- both read zero bytes and not ok. */
void test_psram_is_skipped_when_the_build_never_brought_it_up( void )
{
    bsp_memory_report_t memory = healthy_memory();
    memory.psram_enabled = false;
    memory.psram_bytes = 0;
    memory.psram_ok = false;
    BSP_MemoryCheck_ExpectAndReturn( memory );

    const char *output = run_step( STEP_PSRAM );

    TEST_ASSERT_NOT_NULL( strstr( output, "SKIP" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "FORGIX_QSPI_PSRAM off" ) );
    TEST_ASSERT_NULL( strstr( output, "FAIL" ) );
}


/* The first chunk of sweep 2 is where a smaller die aliasing the window shows
   up: sweep 1 finished writing the whole range first, so the early pattern the
   verify expects has been overwritten by a later chunk that landed on it. */
void test_psram_fails_when_a_verify_chunk_is_lost( void )
{
    BSP_MemoryCheck_ExpectAndReturn( healthy_memory() );
    BSP_MemoryPsramIdentify_ExpectAndReturn( healthy_identity() );
    sweep_fail_at_call = 32;
    sweep_fail_address = 0x15000000u;
    BSP_MemoryPsramSweepChunk_StubWithCallback( sweep_callback );

    const char *output = run_step( STEP_PSRAM );

    TEST_ASSERT_NOT_NULL( strstr( output, "FAIL" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "2048KiB sweep 2/3 LOST at 0x15000000" ) );
}


/* The very last chunk still decides the verdict -- the pass/fail choice on the
   final pass is a boundary its own test has to sit on. */
void test_psram_fails_on_the_final_chunk_of_the_last_sweep( void )
{
    BSP_MemoryCheck_ExpectAndReturn( healthy_memory() );
    BSP_MemoryPsramIdentify_ExpectAndReturn( healthy_identity() );
    sweep_fail_at_call = 95;
    sweep_fail_address = 0x151ffffcu;
    BSP_MemoryPsramSweepChunk_StubWithCallback( sweep_callback );

    const char *output = run_step( STEP_PSRAM );

    TEST_ASSERT_NOT_NULL( strstr( output, "FAIL" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "sweep 3/3 LOST at 0x151FFFFC" ) );
}


/* A write chunk cannot fail on real hardware, but the step does not
   special-case it, and the mock keeps the branch honest. */
void test_psram_fails_during_the_write_sweep( void )
{
    BSP_MemoryCheck_ExpectAndReturn( healthy_memory() );
    BSP_MemoryPsramIdentify_ExpectAndReturn( healthy_identity() );
    sweep_fail_at_call = 0;
    sweep_fail_address = 0x15000000u;
    BSP_MemoryPsramSweepChunk_StubWithCallback( sweep_callback );

    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_PSRAM ), "sweep 1/3 LOST" ) );
}


/* The chunk count follows the reported size: a 1 MiB report sweeps 16 chunks
   per pass, and the order assert inside the callback holds for that span. */
void test_psram_sweeps_the_size_the_report_declares( void )
{
    bsp_memory_report_t memory = healthy_memory();
    memory.psram_bytes = 1024u * 1024u;
    BSP_MemoryCheck_ExpectAndReturn( memory );
    BSP_MemoryPsramIdentify_ExpectAndReturn( healthy_identity() );
    sweep_chunks_expected = 16;
    BSP_MemoryPsramSweepChunk_StubWithCallback( sweep_callback );

    const char *output = run_step( STEP_PSRAM );

    TEST_ASSERT_NOT_NULL( strstr( output, "PASS" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "1024KiB sweep held" ) );
}


/* A window that failed to come back is not a memory to sweep. The identity is
   still reported -- it was read before the re-entry failed -- and the strict
   mocks prove the sweep never ran. */
void test_psram_fails_when_qpi_reentry_fails_and_never_sweeps( void )
{
    BSP_MemoryCheck_ExpectAndReturn( healthy_memory() );
    bsp_memory_psram_identity_t identity = healthy_identity();
    identity.restored = false;
    BSP_MemoryPsramIdentify_ExpectAndReturn( identity );

    const char *output = run_step( STEP_PSRAM );

    TEST_ASSERT_NOT_NULL( strstr( output, "FAIL" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "kgd=0B eid=43; boot POST restore failed" ) );
}


/* Nothing should global-reset a device that reported no usable size; the
   strict mocks prove the identity read never ran either. */
void test_psram_fails_when_the_reported_size_is_too_small_to_sweep( void )
{
    bsp_memory_report_t memory = healthy_memory();
    memory.psram_bytes = 1024u;
    BSP_MemoryCheck_ExpectAndReturn( memory );

    const char *output = run_step( STEP_PSRAM );

    TEST_ASSERT_NOT_NULL( strstr( output, "FAIL" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "1KiB reported; too small to sweep" ) );
}




/***** temperature, USB and watchdog *****/


void test_temperature_passes_inside_the_band_and_formats_a_negative_reading( void )
{
    bsp_adc_temperature_t sample = { .raw = 800, .milli_celsius = 24500 };
    BSP_AdcTemperature_ExpectAndReturn( sample );
    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_TEMPERATURE ), "24.5C" ) );

    sample.milli_celsius = -5500;
    BSP_AdcTemperature_ExpectAndReturn( sample );
    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_TEMPERATURE ), "-5.5C" ) );

    /* Truncation toward zero used to eat the sign here and print "0.5C" -- a
       wrong reading, on the one part of the scale where which side of freezing
       the board is on actually matters. */
    sample.milli_celsius = -500;
    BSP_AdcTemperature_ExpectAndReturn( sample );
    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_TEMPERATURE ), "-0.5C" ) );
}


/* A reading pinned at a rail is the fault a band catches; the absolute figure is
   several degrees out on a good day and is not worth asserting on. */
void test_temperature_fails_outside_the_plausible_band( void )
{
    bsp_adc_temperature_t cold = { .raw = 0, .milli_celsius = -60000 };
    BSP_AdcTemperature_ExpectAndReturn( cold );
    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_TEMPERATURE ), "FAIL" ) );

    bsp_adc_temperature_t hot = { .raw = 4095, .milli_celsius = 120000 };
    BSP_AdcTemperature_ExpectAndReturn( hot );
    TEST_ASSERT_NOT_NULL( strstr( run_step( STEP_TEMPERATURE ), "FAIL" ) );
}


/* Two samples twenty milliseconds apart: a single frame number proves nothing. */
void test_usb_passes_when_the_frame_counter_advances( void )
{
    MOCK_BSP_UsbSetHealth( usb_health( 100, true, false, 256 ) );
    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_single( STEP_USB );
    activity->start();
    MOCK_BSP_ConsoleReset();

    TEST_ASSERT_TRUE( activity->poll() );
    TEST_ASSERT_TRUE( activity->poll() );

    MOCK_BSP_TimeSetMs( 1020 );
    MOCK_BSP_UsbSetHealth( usb_health( 120, true, false, 256 ) );
    TEST_ASSERT_FALSE( activity->poll() );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "sof advancing" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "PASS" ) );
}


void test_usb_fails_when_the_frame_counter_is_frozen( void )
{
    MOCK_BSP_UsbSetHealth( usb_health( 100, true, false, 256 ) );
    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_single( STEP_USB );
    activity->start();
    MOCK_BSP_ConsoleReset();

    TEST_ASSERT_TRUE( activity->poll() );
    MOCK_BSP_TimeSetMs( 1020 );
    TEST_ASSERT_FALSE( activity->poll() );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "sof FROZEN" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "FAIL" ) );
}


void test_usb_fails_on_each_unhealthy_condition_in_isolation( void )
{
    run_usb_step_with( false, false, 256, 120 );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "FAIL" ) );

    run_usb_step_with( true, true, 256, 120 );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "FAIL" ) );

    run_usb_step_with( true, false, 0, 120 );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "FAIL" ) );
}


void test_watchdog_passes_after_a_clean_power_up( void )
{
    application_diagnostics_boot_reason_ExpectAndReturn( BSP_BOOT_POWER_ON );

    const char *output = run_step( STEP_WATCHDOG );

    TEST_ASSERT_NOT_NULL( strstr( output, "PASS" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "last boot power-on" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "marker readback ok" ) );
}


/* The whole watchdog diagnosis rests on that scratch register holding a value
   across a reset, so a register that accepts a write and drops it has to be a
   failure and not a footnote. */
void test_watchdog_fails_when_the_marker_does_not_read_back( void )
{
    application_diagnostics_boot_reason_ExpectAndReturn( BSP_BOOT_POWER_ON );
    MOCK_BSP_WatchdogSetMarkerReadbackFaulty( true );

    const char *output = run_step( STEP_WATCHDOG );

    TEST_ASSERT_NOT_NULL( strstr( output, "FAIL" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "marker readback BAD" ) );
}


/* The board is plainly running now, but something stopped feeding the loop and
   the retained marker is the only witness to where. */
void test_watchdog_fails_when_the_previous_boot_was_forced_by_the_watchdog( void )
{
    application_diagnostics_boot_reason_ExpectAndReturn( BSP_BOOT_WATCHDOG );

    const char *output = run_step( STEP_WATCHDOG );

    TEST_ASSERT_NOT_NULL( strstr( output, "FAIL" ) );
    TEST_ASSERT_NOT_NULL( strstr( output, "last boot watchdog" ) );
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


/* Pins the property the sweep exists for: every chunk of a pass runs before any
   chunk of the next, in order. A configured call index can be made to fail,
   standing in for a bad cell or an aliased address at that point in the run. */
static bsp_memory_sweep_result_t sweep_callback( bsp_memory_sweep_op op, uint32_t chunk_index,
                                                 int num_calls )
{
    TEST_ASSERT_EQUAL_UINT32( (uint32_t) num_calls / sweep_chunks_expected, (uint32_t) op );
    TEST_ASSERT_EQUAL_UINT32( (uint32_t) num_calls % sweep_chunks_expected, chunk_index );

    bsp_memory_sweep_result_t result = ok_sweep();
    if ( num_calls == sweep_fail_at_call )
    {
        result.ok = false;
        result.fail_address = sweep_fail_address;
    }
    return result;
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


/* Each of the four conditions on its own, because a link can be unhealthy in
   exactly one way and an && that is only ever tested with everything wrong at
   once would never notice three of them. */
static void run_usb_step_with( bool connected, bool suspended, uint32_t write_available,
                               uint32_t second_frame )
{
    MOCK_BSP_UsbSetHealth( usb_health( 100, connected, suspended, write_available ) );
    MOCK_BSP_TimeSetMs( 1000 );
    const application_activity_t *activity = application_ibit_single( STEP_USB );
    activity->start();
    MOCK_BSP_ConsoleReset();

    TEST_ASSERT_TRUE( activity->poll() );
    MOCK_BSP_TimeSetMs( 1020 );
    MOCK_BSP_UsbSetHealth( usb_health( second_frame, connected, suspended, write_available ) );
    TEST_ASSERT_FALSE( activity->poll() );
}
