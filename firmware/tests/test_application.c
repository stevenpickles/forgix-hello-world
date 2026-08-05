/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "application.h"
#include "application_console.h"
#include "application_diagnostics.h"
#include "mock_bsp_console.h"
#include "mock_bsp_time.h"
#include "mock_bsp_usb.h"
#include "mock_bsp_watchdog.h"
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


static void process( const char *command );

static void expect_memory_report( void );

static bsp_memory_report_t memory_report( void );

static bsp_memory_identity_dump_t identity_dump( void );

static bsp_led_state_t expected_hello_led( void );

static void expect_hello_readback( uint8_t design_id, bsp_led_state_t led );




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


void test_application_init_reports_ready_hardware_and_help( void )
{
    bsp_init_result_t result = {
        .configured = true,
        .design_id = BSP_FPGA_DESIGN_ID,
        .ready = true,
        .cdone = true,
        .status_pin = true,
    };

    expect_memory_report();
    application_init( &result );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "configuration=ok" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "runtime=ready" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "hello | color" ) );
}


void test_application_init_preserves_diagnostics_when_hardware_is_unavailable( void )
{
    bsp_init_result_t result = {
        .configured = false,
        .design_id = 0,
        .ready = false,
        .cdone = false,
        .status_pin = false,
    };

    expect_memory_report();
    application_init( &result );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "configuration=failed" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "runtime=unavailable" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "runtime commands are disabled" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "hello | color" ) );
}


void test_empty_command_has_no_effect( void )
{
    process( " \t" );
    TEST_ASSERT_EQUAL_STRING( "", MOCK_BSP_ConsoleOutput() );
}


void test_help_remains_available_without_fpga_access( void )
{
    process( "help" );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "hello | color" ) );
}


/* The menu is how a user reaches the tests that diagnose a dead FPGA, so getting
   back to it must not be one of the things a dead FPGA takes away. */
void test_menu_returns_to_the_ui_without_consulting_the_fpga( void )
{
    application_ui_enter_menu_Expect();
    process( "menu" );
    TEST_ASSERT_EQUAL_STRING( "", MOCK_BSP_ConsoleOutput() );
}


void test_hardware_command_is_rejected_when_fpga_is_unavailable( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( false );

    process( "hello" );

    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(), "FPGA is not configured and responding" ) );
}


void test_color_uses_default_brightness( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );
    BSP_LedSet_Expect( 1, 2, 3, 255 );

    process( "color 1 2 3" );

    TEST_ASSERT_EQUAL_STRING( "ok\n", MOCK_BSP_ConsoleOutput() );
}


void test_color_forwards_explicit_brightness( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );
    BSP_LedSet_Expect( 4, 5, 6, 7 );

    process( "color 4 5 6 7" );

    TEST_ASSERT_EQUAL_STRING( "ok\n", MOCK_BSP_ConsoleOutput() );
}


void test_color_rejects_values_above_a_byte_without_writing_hardware( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );

    process( "color 1 2 256" );

    TEST_ASSERT_EQUAL_STRING( "error: values must be 0..255\n", MOCK_BSP_ConsoleOutput() );
}


void test_color_rejects_negative_values_without_writing_hardware( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );

    process( "color -1 2 3" );

    TEST_ASSERT_EQUAL_STRING( "error: values must be 0..255\n", MOCK_BSP_ConsoleOutput() );
}


void test_color_rejects_non_numeric_values_without_writing_hardware( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );

    process( "color red 2 3" );

    TEST_ASSERT_EQUAL_STRING( "error: values must be 0..255\n", MOCK_BSP_ConsoleOutput() );
}


void test_color_rejects_trailing_characters_without_writing_hardware( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );

    process( "color 1 2 3x" );

    TEST_ASSERT_EQUAL_STRING( "error: values must be 0..255\n", MOCK_BSP_ConsoleOutput() );
}


void test_color_rejects_the_wrong_argument_count( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );

    process( "color 1 2" );

    TEST_ASSERT_EQUAL_STRING( "error: invalid command (try help)\n", MOCK_BSP_ConsoleOutput() );
}


void test_hello_programs_and_verifies_the_expected_led_state( void )
{
    expect_hello_readback( BSP_FPGA_DESIGN_ID, expected_hello_led() );

    process( "hello" );

    TEST_ASSERT_EQUAL_STRING( "Hello from RP2354 -> FPGA B6\n", MOCK_BSP_ConsoleOutput() );
}


void test_hello_reports_failed_readback( void )
{
    bsp_led_state_t led = { 0 };
    expect_hello_readback( 0, led );

    process( "hello" );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "hello readback failed" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "id=00" ) );
}


void test_hello_reports_each_led_readback_mismatch( void )
{
    const char *field_names[] = { "red", "green", "blue", "brightness", "enabled" };
    bsp_led_state_t mismatches[ 5 ];

    for ( uint32_t index = 0; index < 5u; ++index )
    {
        mismatches[ index ] = expected_hello_led();
    }
    mismatches[ 0 ].red = 1;
    mismatches[ 1 ].green = 254;
    mismatches[ 2 ].blue = 254;
    mismatches[ 3 ].brightness = 63;
    mismatches[ 4 ].enabled = false;

    for ( uint32_t index = 0; index < 5u; ++index )
    {
        expect_hello_readback( BSP_FPGA_DESIGN_ID, mismatches[ index ] );
        process( "hello" );

        TEST_ASSERT_NOT_NULL_MESSAGE( strstr( MOCK_BSP_ConsoleOutput(), "hello readback failed" ),
                                      field_names[ index ] );
        MOCK_BSP_ConsoleReset();
    }
}


void test_off_disables_the_led( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );
    BSP_LedOff_Expect();

    process( "off" );

    TEST_ASSERT_EQUAL_STRING( "ok\n", MOCK_BSP_ConsoleOutput() );
}


void test_status_reports_fpga_and_button_state( void )
{
    bsp_button_state_t button = { .level = 0x03, .count = 7 };
    BSP_FpgaIsReady_ExpectAndReturn( true );
    BSP_ButtonGetState_ExpectAndReturn( button );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_FpgaReadStatus_ExpectAndReturn( 0x01 );
    BSP_FpgaStatusPin_ExpectAndReturn( true );

    process( "status" );

    TEST_ASSERT_EQUAL_STRING( "id=B6 status=01 button=03 count=7 fpga_status=1\n",
                              MOCK_BSP_ConsoleOutput() );
}


void test_console_control_commands_remain_available_without_fpga_access( void )
{
    const char *commands[] = {
        "echo on", "echo off", "watch 1", "watch 3600", "watch off", "quiet", "interactive",
    };

    for ( uint32_t index = 0; index < (uint32_t) ( sizeof commands / sizeof commands[ 0 ] );
          ++index )
    {
        process( commands[ index ] );
        TEST_ASSERT_EQUAL_STRING( "ok\n", MOCK_BSP_ConsoleOutput() );
        MOCK_BSP_ConsoleReset();
    }
}


void test_console_control_commands_report_invalid_usage( void )
{
    const char *commands[] = {
        "echo",    "echo maybe", "echo on extra",   "watch",       "watch nope",        "watch 1x",
        "watch 0", "watch 3601", "watch off extra", "quiet extra", "interactive extra",
    };

    for ( uint32_t index = 0; index < (uint32_t) ( sizeof commands / sizeof commands[ 0 ] );
          ++index )
    {
        process( commands[ index ] );
        TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "error:" ) );
        MOCK_BSP_ConsoleReset();
    }
}


void test_status_reports_unavailable_hardware_without_accessing_registers( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( false );

    process( "status" );

    TEST_ASSERT_EQUAL_STRING( "status unavailable: FPGA is not configured and responding\n",
                              MOCK_BSP_ConsoleOutput() );
}


void test_diag_reports_the_last_reset_and_stays_available_without_fpga_access( void )
{
    MOCK_BSP_WatchdogSetBootReason( BSP_BOOT_WATCHDOG );
    MOCK_BSP_WatchdogSetRetained( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE, 0, 0, 0 );
    expect_memory_report();

    process( "diag" );

    /* both QSPI memories, so a shared-bus fault is visible without a reboot */
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "flash=2048KiB ok=1" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "psram=8192KiB ok=1" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "diag: boot=" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "uptime=" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "fpga_reconfig=" ) );
}


/* No BSP_FpgaIsReady expectation is queued: a consult would fail through
   CMock, which is the FPGA-down guarantee for the one command that exists to
   interrogate a distrusted board. */
void test_memid_dumps_every_response_byte_without_consulting_the_fpga( void )
{
    bsp_memory_identity_dump_t dump = identity_dump();

    BSP_MemoryIdentityDump_ExpectAndReturn( dump );

    process( "memid" );

    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(), "cs0 flash 9F: EF 40 15 00 00 00 00 00" ) );
    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(), "cs1 psram 9F @25000kHz: 00 00 00 00 0D 0B 43 FF" ) );
    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(), "cs1 psram 9F @5000kHz: 00 00 00 00 0D 0B 43 FE" ) );
    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(), "cs1 psram 9F @1000kHz: 00 00 00 00 0D 0B 43 FD" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "qpi re-entry: ok" ) );
}


void test_memid_reports_a_failed_qpi_reentry( void )
{
    bsp_memory_identity_dump_t dump = identity_dump();
    dump.restored = false;

    BSP_MemoryIdentityDump_ExpectAndReturn( dump );

    process( "memid" );

    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(),
                "error: qpi re-entry failed; psram is down until the next check" ) );
}


void test_memid_says_when_the_image_has_no_psram_support( void )
{
    bsp_memory_identity_dump_t dump = identity_dump();
    dump.psram_probed = false;

    BSP_MemoryIdentityDump_ExpectAndReturn( dump );

    process( "memid" );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "cs0 flash 9F: EF 40 15" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(),
                                  "cs1 psram: not probed; this image was built without PSRAM" ) );
    /* the probe lines and the re-entry verdict describe reads that never ran */
    TEST_ASSERT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "cs1 psram 9F" ) );
    TEST_ASSERT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "qpi re-entry" ) );
}


void test_reset_reaches_the_fpga( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );
    BSP_FpgaReset_Expect();

    process( "reset" );

    TEST_ASSERT_EQUAL_STRING( "ok\n", MOCK_BSP_ConsoleOutput() );
}


void test_unknown_command_is_rejected( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );

    process( "unknown" );

    TEST_ASSERT_EQUAL_STRING( "error: invalid command (try help)\n", MOCK_BSP_ConsoleOutput() );
}


/* The gate-free commands answer their own malformed forms without consulting
   the FPGA at all: no readiness expectation is queued for them, so a consult
   would fail the test through CMock. That is also the FPGA-down guarantee --
   a typo in "help" on a board whose FPGA is dead used to be answered with the
   FPGA error, blaming the hardware for a malformed line on the very commands
   kept alive to diagnose it. */
void test_known_commands_with_extra_arguments_are_rejected( void )
{
    const char *gated_commands[] = { "hello extra", "off extra", "reset extra" };
    const char *gate_free_commands[] = {
        "help extra", "status extra", "diag extra", "memid extra", "menu extra",
    };

    for ( uint32_t index = 0;
          index < (uint32_t) ( sizeof gated_commands / sizeof gated_commands[ 0 ] ); ++index )
    {
        BSP_FpgaIsReady_ExpectAndReturn( true );
        process( gated_commands[ index ] );

        TEST_ASSERT_EQUAL_STRING( "error: invalid command (try help)\n", MOCK_BSP_ConsoleOutput() );
        MOCK_BSP_ConsoleReset();
    }

    for ( uint32_t index = 0;
          index < (uint32_t) ( sizeof gate_free_commands / sizeof gate_free_commands[ 0 ] );
          ++index )
    {
        process( gate_free_commands[ index ] );

        TEST_ASSERT_EQUAL_STRING( "error: invalid command (try help)\n", MOCK_BSP_ConsoleOutput() );
        MOCK_BSP_ConsoleReset();
    }
}



void test_command_tokenization_is_safely_limited_to_the_argument_capacity( void )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );

    process( "unknown 1 2 3 4 5 6" );

    TEST_ASSERT_EQUAL_STRING( "error: invalid command (try help)\n", MOCK_BSP_ConsoleOutput() );
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


static void process( const char *command )
{
    char mutable_command[ 128 ];
    snprintf( mutable_command, sizeof mutable_command, "%s", command );
    application_process_command( mutable_command );
}


static void expect_memory_report( void )
{
    BSP_MemoryCheck_ExpectAndReturn( memory_report() );
}


static bsp_memory_report_t memory_report( void )
{
    bsp_memory_report_t report = {
        .flash_bytes = 2u * 1024u * 1024u,
        .flash_ok = true,
        .psram_bytes = 8u * 1024u * 1024u,
        .psram_ok = true,
    };
    return report;
}


/* Bytes chosen to look like the real investigation: a plausible Winbond flash
   ID on the control line, the observed unexpected PSRAM identity on all three
   rates, and a trailing byte that differs per rate so a test could never pass
   by printing one response three times. */
static bsp_memory_identity_dump_t identity_dump( void )
{
    bsp_memory_identity_dump_t dump = {
        .flash_response = { 0xEF, 0x40, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00 },
        .psram_probed = true,
        .probe_hz = { 25000000u, 5000000u, 1000000u },
        .psram_response =
            {
                { 0x00, 0x00, 0x00, 0x00, 0x0D, 0x0B, 0x43, 0xFF },
                { 0x00, 0x00, 0x00, 0x00, 0x0D, 0x0B, 0x43, 0xFE },
                { 0x00, 0x00, 0x00, 0x00, 0x0D, 0x0B, 0x43, 0xFD },
            },
        .restored = true,
    };
    return dump;
}


static bsp_led_state_t expected_hello_led( void )
{
    bsp_led_state_t led = {
        .red = 0,
        .green = 255,
        .blue = 255,
        .brightness = 64,
        .enabled = true,
    };
    return led;
}


static void expect_hello_readback( uint8_t design_id, bsp_led_state_t led )
{
    BSP_FpgaIsReady_ExpectAndReturn( true );
    BSP_LedSet_Expect( 0, 255, 255, 64 );
    BSP_FpgaPing_ExpectAndReturn( design_id );
    BSP_LedGet_ExpectAndReturn( led );
}
