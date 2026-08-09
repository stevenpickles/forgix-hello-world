/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "unity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "application_diagnostics.h"
#include "application_diagnostics_led.h"
#include "application_diagnostics_report.h"
#include "application_time.h"
#include "mock_bsp_console.h"
#include "mock_bsp_time.h"
#include "mock_bsp_usb.h"
#include "mock_bsp_watchdog.h"
#include "mock_auto_bsp_fpga.h"
#include "mock_auto_bsp_led.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


enum
{
    BRIGHTNESS = 64
};

/* The blink code repeats, because it cannot be replayed: a power cycle to watch
   it again would reset the scratch registers it is reporting. */
enum
{
    BOOT_REPEATS = 3
};




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bsp_led_state_t led_state( uint8_t red, uint8_t green, uint8_t blue, bool enabled );

static bsp_usb_health_t health_of( bool connected, bool suspended, uint32_t write_available,
                                   uint32_t activity_count, uint32_t tx_complete_count,
                                   uint32_t frame_number );

static void start_usb_at( uint32_t now_ms );

static void start_led_only( bsp_boot_reason reason, uint32_t marker, uint8_t red, uint8_t green,
                            uint8_t blue, uint32_t blinks, uint8_t rest_red, uint8_t rest_green,
                            uint8_t rest_blue );

static void poll_at( uint32_t now_ms );

static void expect_sample( uint8_t red, uint8_t green, uint8_t blue );




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


void test_start_reports_the_retained_watchdog_evidence_and_arms_the_watchdog( void )
{
    MOCK_BSP_WatchdogSetBootReason( BSP_BOOT_WATCHDOG );
    MOCK_BSP_WatchdogSetRetained( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE, 612, 44,
                                  0x00010001 );

    start_usb_at( 0 );

    TEST_ASSERT_EQUAL_STRING( "diag: boot=watchdog marker=3 loop=612 usb=44 health=00010001\n",
                              MOCK_BSP_ConsoleOutput() );
    TEST_ASSERT_TRUE( MOCK_BSP_WatchdogStarted() );
    TEST_ASSERT_EQUAL_UINT32( APPLICATION_DIAGNOSTICS_WATCHDOG_TIMEOUT_MS,
                              MOCK_BSP_WatchdogTimeoutMs() );
    TEST_ASSERT_EQUAL_UINT32( APPLICATION_DIAGNOSTICS_MARKER_LOOP, MOCK_BSP_WatchdogMarker() );
    for ( uint32_t slot = 0; slot < BSP_WATCHDOG_SNAPSHOT_SLOTS; ++slot )
    {
        TEST_ASSERT_EQUAL_UINT32( 0, MOCK_BSP_WatchdogSnapshot( slot ) );
    }
}


void test_start_names_every_boot_reason_on_the_console( void )
{
    const struct
    {
        bsp_boot_reason reason;
        const char *name;
    } cases[] = {
        { BSP_BOOT_POWER_ON, "boot=power-on" },
        { BSP_BOOT_BROWNOUT, "boot=brownout" },
        { BSP_BOOT_WATCHDOG, "boot=watchdog" },
        { BSP_BOOT_OTHER, "boot=other" },
    };

    for ( uint32_t index = 0; index < (uint32_t) ( sizeof cases / sizeof cases[ 0 ] ); ++index )
    {
        MOCK_BSP_ConsoleReset();
        MOCK_BSP_WatchdogReset();
        MOCK_BSP_WatchdogSetBootReason( cases[ index ].reason );

        start_usb_at( 0 );

        TEST_ASSERT_NOT_NULL_MESSAGE( strstr( MOCK_BSP_ConsoleOutput(), cases[ index ].name ),
                                      cases[ index ].name );
    }
}


void test_usb_free_image_blinks_a_white_power_on_code_and_still_prints_it( void )
{
    start_led_only( BSP_BOOT_POWER_ON, 0, 255, 255, 255, 1, 0, 0, 255 );

    /* The print costs nothing with no stdio backend linked, and carries the
       report over UART when the image is built with FORGIX_DIAGNOSTIC_UART. */
    TEST_ASSERT_EQUAL_STRING( "diag: boot=power-on marker=0 loop=0 usb=0 health=00000000\n",
                              MOCK_BSP_ConsoleOutput() );
    /* one leading gap, then per pass: an on/off pair per blink plus a gap */
    TEST_ASSERT_EQUAL_UINT32( 1 + BOOT_REPEATS * ( 1 * 2 + 1 ), MOCK_BSP_TimeSleepCount() );
    TEST_ASSERT_TRUE( MOCK_BSP_WatchdogStarted() );
}


/* The per-second line is the MCU-liveness proof the LED cannot give: it depends
   only on the foreground loop, so the last logged second dates a freeze exactly. */
void test_usb_free_image_logs_a_line_every_second( void )
{
    start_led_only( BSP_BOOT_POWER_ON, 0, 255, 255, 255, 1, 0, 0, 255 );
    BSP_LedOff_Expect();
    poll_at( 250 );
    MOCK_BSP_ConsoleReset();

    BSP_LedSet_Expect( 0, 0, 255, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_LedGet_ExpectAndReturn( led_state( 0, 0, 255, true ) );
    poll_at( 1000 );

    TEST_ASSERT_EQUAL_STRING( "diag: t=1s led=1 fpga_fail=0 fpga_reconfig=0 marker=6\n",
                              MOCK_BSP_ConsoleOutput() );
}


/* Each boot reason keeps its resting heartbeat color for the whole run, so the
   verdict is readable hours later without having caught the blink code. */
void test_usb_free_image_blinks_brownout_in_yellow_and_rests_yellow( void )
{
    start_led_only( BSP_BOOT_BROWNOUT, 0, 255, 255, 0, 2, 255, 255, 0 );
}


void test_usb_free_image_blinks_unclassified_resets_in_cyan_and_rests_cyan( void )
{
    start_led_only( BSP_BOOT_OTHER, 0, 0, 255, 255, 3, 0, 255, 255 );
}


void test_usb_free_image_blinks_the_retained_marker_in_red_and_rests_red( void )
{
    start_led_only( BSP_BOOT_WATCHDOG, APPLICATION_DIAGNOSTICS_MARKER_COMMAND, 255, 0, 0, 4, 255, 0,
                    0 );
}


void test_watchdog_blink_count_is_clamped_to_a_readable_range( void )
{
    start_led_only( BSP_BOOT_WATCHDOG, 0, 255, 0, 0, 1, 255, 0, 0 );
    start_led_only( BSP_BOOT_WATCHDOG, 20, 255, 0, 0, 8, 255, 0, 0 );
}


void test_diag_report_lists_the_previous_boot_and_the_live_counters( void )
{
    MOCK_BSP_WatchdogSetBootReason( BSP_BOOT_BROWNOUT );
    MOCK_BSP_WatchdogSetRetained( 2, 7, 8, 9 );
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );
    MOCK_BSP_UsbSetHealth( health_of( true, false, 32, 5, 0, 100 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 1000 );
    MOCK_BSP_ConsoleReset();

    application_diagnostics_print_report();

    TEST_ASSERT_EQUAL_STRING( "diag: boot=brownout marker=2 loop=7 usb=8 health=00000009\n"
                              "diag: uptime=1s connected=1 suspended=0 write=32 activity=5 sof=100 "
                              "fpga_fail=0 fpga_cdone=0 fpga_ping=0 fpga_led=0 fpga_reconfig=0\n",
                              MOCK_BSP_ConsoleOutput() );
}


/* The attribution is what turned "the check failed once a boot" into a
   measurement: each failing sample charges exactly one of the three term
   counters, named after the first term that failed, and the report is where a
   bench session reads them back. */
void test_diag_report_attributes_each_failure_to_the_term_that_failed( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    /* one CDONE failure, then a ping failure, then a readback failure --
       scattered across passing samples so the debounce never engages */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( false );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );
    expect_sample( 0, 255, 0 );
    poll_at( 2000 );

    BSP_LedOff_Expect();
    poll_at( 2250 );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( 0x00 );
    poll_at( 3000 );

    BSP_LedOff_Expect();
    poll_at( 3250 );
    expect_sample( 0, 255, 0 );
    poll_at( 4000 );

    BSP_LedOff_Expect();
    poll_at( 4250 );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_LedGet_ExpectAndReturn( led_state( 1, 2, 3, true ) );
    poll_at( 5000 );
    MOCK_BSP_ConsoleReset();

    application_diagnostics_print_report();

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(),
                                  "fpga_fail=3 fpga_cdone=1 fpga_ping=1 fpga_led=1 "
                                  "fpga_reconfig=0" ) );
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


static bsp_led_state_t led_state( uint8_t red, uint8_t green, uint8_t blue, bool enabled )
{
    bsp_led_state_t state = {
        .red = red,
        .green = green,
        .blue = blue,
        .brightness = BRIGHTNESS,
        .enabled = enabled,
    };
    return state;
}

static bsp_usb_health_t health_of( bool connected, bool suspended, uint32_t write_available,
                                   uint32_t activity_count, uint32_t tx_complete_count,
                                   uint32_t frame_number )
{
    bsp_usb_health_t health = {
        .connected = connected,
        .suspended = suspended,
        .write_available = write_available,
        .activity_count = activity_count,
        .tx_complete_count = tx_complete_count,
        .frame_number = frame_number,
    };
    return health;
}

/* The heartbeat starts blue in both images: health has not been sampled yet, so
   the host counts as absent. */
static void start_usb_at( uint32_t now_ms )
{
    MOCK_BSP_UsbSetPresent( true );
    MOCK_BSP_TimeSetMs( now_ms );
    BSP_LedSet_Expect( 0, 0, 255, BRIGHTNESS );
    application_diagnostics_start();
}

static void start_led_only( bsp_boot_reason reason, uint32_t marker, uint8_t red, uint8_t green,
                            uint8_t blue, uint32_t blinks, uint8_t rest_red, uint8_t rest_green,
                            uint8_t rest_blue )
{
    MOCK_BSP_UsbSetPresent( false );
    MOCK_BSP_WatchdogSetBootReason( reason );
    MOCK_BSP_WatchdogSetRetained( marker, 0, 0, 0 );
    MOCK_BSP_TimeSetMs( 0 );

    BSP_LedOff_Expect();
    for ( uint32_t pass = 0; pass < BOOT_REPEATS; ++pass )
    {
        for ( uint32_t blink = 0; blink < blinks; ++blink )
        {
            BSP_LedSet_Expect( red, green, blue, BRIGHTNESS );
            BSP_LedOff_Expect();
        }
    }
    BSP_LedSet_Expect( rest_red, rest_green, rest_blue, BRIGHTNESS );
    application_diagnostics_start();
}

static void poll_at( uint32_t now_ms )
{
    MOCK_BSP_TimeSetMs( now_ms );
    application_diagnostics_poll();
}

/* Advances to the first one-second sample with the supplied health, leaving the
   heartbeat lit so the color under test is observable. */
static void expect_sample( uint8_t red, uint8_t green, uint8_t blue )
{
    BSP_LedSet_Expect( red, green, blue, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_LedGet_ExpectAndReturn( led_state( red, green, blue, true ) );
}
