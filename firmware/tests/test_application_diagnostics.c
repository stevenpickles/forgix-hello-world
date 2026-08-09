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

static void run_readback_mismatch( bsp_led_state_t readback, const char *field_name );




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


/* The snapshot is taken before the watchdog is armed and has to stay readable
   afterwards. Arming writes the scratch word watchdog_enable_caused_reboot
   consults, so anything that asks the BSP again later is told every board came
   back from a watchdog reset -- which is precisely what the built-in test
   reported on hardware that had powered up cleanly. */
void test_boot_reason_is_published_from_the_snapshot_taken_before_the_watchdog_was_armed( void )
{
    start_led_only( BSP_BOOT_BROWNOUT, 0, 255, 255, 0, 2, 255, 255, 0 );

    TEST_ASSERT_TRUE( MOCK_BSP_WatchdogStarted() );
    TEST_ASSERT_EQUAL_INT( BSP_BOOT_BROWNOUT, application_diagnostics_boot_reason() );
}


/* The bench regression this debounce exists for: the bit-banged bus misreads
   about one sample per boot, and revoking on the first failure gated the shell
   until reboot every time. A lone failing sample is counted but not acted on:
   the strict mock proves neither the latch nor recovery was touched. */
void test_a_single_failing_sample_is_counted_but_not_acted_on( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( false );
    poll_at( 1000 );

    /* the fault is still counted, so a run records it without disturbing it */
    TEST_ASSERT_EQUAL_UINT32( 1u << 19, MOCK_BSP_WatchdogSnapshot( 2 ) & ( 0x7fu << 19 ) );
    TEST_ASSERT_EQUAL_UINT32( 0, MOCK_BSP_WatchdogSnapshot( 2 ) & ( 0x3fu << 26 ) );
}


/* A passing sample between failures restarts the debounce, so scattered
   transients never accumulate into a revocation however many a boot collects. */
void test_a_passing_sample_between_failures_restarts_the_debounce( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( false );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( false );
    poll_at( 2000 );

    BSP_LedOff_Expect();
    poll_at( 2250 );
    expect_sample( 0, 255, 0 );
    poll_at( 3000 );

    /* two more failures after the pass: still below three consecutive */
    BSP_LedOff_Expect();
    poll_at( 3250 );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( false );
    poll_at( 4000 );

    BSP_LedOff_Expect();
    poll_at( 4250 );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( false );
    poll_at( 5000 );

    TEST_ASSERT_EQUAL_UINT32( 4u << 19, MOCK_BSP_WatchdogSnapshot( 2 ) & ( 0x7fu << 19 ) );
}


/* Guards against "only falsify once" regressions: the latch must be written on
   every failing sample, not just the first, since a reconfiguration between
   samples could have set it true again in the meantime. */
/* The third consecutive failure is where the fault becomes actionable: a real
   fault fails every sample, so it reaches three in two extra seconds, while a
   misread never does. Marking then continues on every failing sample past the
   threshold, since a reconfiguration between samples could have restored the
   latch in the meantime. */
void test_the_third_consecutive_failure_revokes_readiness_and_marking_continues( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( false );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( false );
    poll_at( 2000 );

    BSP_LedOff_Expect();
    poll_at( 2250 );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( false );
    BSP_FpgaMarkUnresponsive_Expect();
    BSP_FpgaAutoReconfigureEnabled_ExpectAndReturn( false );
    poll_at( 3000 );

    BSP_LedOff_Expect();
    poll_at( 3250 );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( false );
    BSP_FpgaMarkUnresponsive_Expect();
    BSP_FpgaAutoReconfigureEnabled_ExpectAndReturn( false );
    poll_at( 4000 );

    TEST_ASSERT_EQUAL_UINT32( 4u << 19, MOCK_BSP_WatchdogSnapshot( 2 ) & ( 0x7fu << 19 ) );
}


void test_poll_feeds_the_watchdog_and_marks_the_loop_before_any_deadline( void )
{
    start_usb_at( 0 );

    poll_at( 10 );

    TEST_ASSERT_EQUAL_UINT32( 1, MOCK_BSP_WatchdogFeedCount() );
    TEST_ASSERT_EQUAL_UINT32( APPLICATION_DIAGNOSTICS_MARKER_LOOP, MOCK_BSP_WatchdogMarker() );
}


void test_sample_shows_green_and_snapshots_health_while_traffic_advances( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 1000 );

    TEST_ASSERT_EQUAL_UINT32( 1, MOCK_BSP_WatchdogSnapshot( 0 ) );
    TEST_ASSERT_EQUAL_UINT32( 5, MOCK_BSP_WatchdogSnapshot( 1 ) );
    TEST_ASSERT_EQUAL_UINT32( 100u | ( 1u << 16 ), MOCK_BSP_WatchdogSnapshot( 2 ) );
    TEST_ASSERT_TRUE(
        MOCK_BSP_WatchdogMarkerWasWritten( APPLICATION_DIAGNOSTICS_MARKER_USB_SNAPSHOT ) );
    TEST_ASSERT_TRUE(
        MOCK_BSP_WatchdogMarkerWasWritten( APPLICATION_DIAGNOSTICS_MARKER_FPGA_CHECK ) );
    TEST_ASSERT_EQUAL_UINT32( APPLICATION_DIAGNOSTICS_MARKER_LOOP, MOCK_BSP_WatchdogMarker() );
}


void test_lost_configuration_reconfigures_and_flies_the_recovery_signature( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    /* two failing samples ride out the debounce; recovery acts on the third */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( false );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( false );
    poll_at( 2000 );

    BSP_LedOff_Expect();
    poll_at( 2250 );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( false );
    BSP_FpgaMarkUnresponsive_Expect();
    BSP_FpgaAutoReconfigureEnabled_ExpectAndReturn( true );
    BSP_FpgaReconfigure_ExpectAndReturn( true );
    BSP_LedSet_Expect( 255, 255, 255, BRIGHTNESS );
    poll_at( 3000 );

    TEST_ASSERT_EQUAL_UINT32( 100u | ( 1u << 16 ) | ( 3u << 19 ) | ( 1u << 26 ),
                              MOCK_BSP_WatchdogSnapshot( 2 ) );

    /* the signature survives the following toggles, then healthy color resumes */
    BSP_LedOff_Expect();
    poll_at( 3250 );
    BSP_LedSet_Expect( 255, 255, 255, BRIGHTNESS );
    poll_at( 3500 );
    BSP_LedOff_Expect();
    poll_at( 3750 );
    BSP_LedSet_Expect( 255, 255, 255, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_LedGet_ExpectAndReturn( led_state( 255, 255, 255, true ) );
    poll_at( 4000 );
    BSP_LedOff_Expect();
    poll_at( 4250 );
    BSP_LedSet_Expect( 255, 255, 255, BRIGHTNESS );
    poll_at( 4500 );
    BSP_LedOff_Expect();
    poll_at( 4750 );
    expect_sample( 0, 255, 0 );
    poll_at( 5000 );
}


/* Reconfiguration used to repaint unconditionally, so a background recovery
   punched solid white through whatever a light show or an IBIT LED step was
   painting -- and, released, that white was never refreshed, so it stuck until
   the activity's next frame. While the LED is released the fresh registers are
   the new owner's to fill; the signature arrives with the handover instead. */
void test_reconfiguration_while_the_led_is_released_writes_nothing( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    application_diagnostics_release_led();
    poll_at( 500 );
    poll_at( 750 );

    /* No LED expectation is queued anywhere here: a repaint on this path would
       fail the test as an unexpected BSP_LedSet call. Two failing samples ride
       out the debounce first. */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    BSP_FpgaCdone_ExpectAndReturn( false );
    poll_at( 1000 );
    BSP_FpgaCdone_ExpectAndReturn( false );
    poll_at( 2000 );
    BSP_FpgaCdone_ExpectAndReturn( false );
    BSP_FpgaMarkUnresponsive_Expect();
    BSP_FpgaAutoReconfigureEnabled_ExpectAndReturn( true );
    BSP_FpgaReconfigure_ExpectAndReturn( true );
    poll_at( 3000 );

    TEST_ASSERT_EQUAL_UINT32( 100u | ( 1u << 16 ) | ( 3u << 19 ) | ( 1u << 26 ),
                              MOCK_BSP_WatchdogSnapshot( 2 ) );

    /* The recovery signature flies from the reclaim onward. */
    BSP_LedSet_Expect( 255, 255, 255, BRIGHTNESS );
    application_diagnostics_reclaim_led();
}


void test_a_wrong_design_id_reconfigures_without_reading_the_led_back( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( 0x00 );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( 0x00 );
    poll_at( 2000 );

    BSP_LedOff_Expect();
    poll_at( 2250 );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( 0x00 );
    BSP_FpgaMarkUnresponsive_Expect();
    BSP_FpgaAutoReconfigureEnabled_ExpectAndReturn( true );
    BSP_FpgaReconfigure_ExpectAndReturn( true );
    BSP_LedSet_Expect( 255, 255, 255, BRIGHTNESS );
    poll_at( 3000 );

    TEST_ASSERT_EQUAL_UINT32( ( 3u << 19 ) | ( 1u << 26 ),
                              MOCK_BSP_WatchdogSnapshot( 2 ) &
                                  ( ( 0x7fu << 19 ) | ( 0x3fu << 26 ) ) );
}


void test_each_led_register_readback_mismatch_is_treated_as_an_fpga_fault( void )
{
    const char *field_names[] = { "red", "green", "blue", "brightness", "enabled" };
    bsp_led_state_t mismatches[ 5 ];

    for ( uint32_t index = 0; index < 5u; ++index )
    {
        mismatches[ index ] = led_state( 0, 255, 0, true );
    }
    mismatches[ 0 ].red = 1;
    mismatches[ 1 ].green = 254;
    mismatches[ 2 ].blue = 1;
    mismatches[ 3 ].brightness = BRIGHTNESS - 1;
    mismatches[ 4 ].enabled = false;

    for ( uint32_t index = 0; index < 5u; ++index )
    {
        run_readback_mismatch( mismatches[ index ], field_names[ index ] );
    }
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

/* A failed reconfiguration must leave the failure counted but fly no recovery
   signature, so the operator can tell "self-healed" from "still broken". */
static void run_readback_mismatch( bsp_led_state_t readback, const char *field_name )
{
    MOCK_BSP_ConsoleReset();
    MOCK_BSP_TimeReset();
    MOCK_BSP_UsbReset();
    MOCK_BSP_WatchdogReset();

    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    /* three consecutive mismatching samples, so the debounced recovery runs
       and fails; each sample rewrites and re-reads the heartbeat color */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_LedGet_ExpectAndReturn( readback );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_LedGet_ExpectAndReturn( readback );
    poll_at( 2000 );

    BSP_LedOff_Expect();
    poll_at( 2250 );
    BSP_LedSet_Expect( 0, 255, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_LedGet_ExpectAndReturn( readback );
    BSP_FpgaMarkUnresponsive_Expect();
    BSP_FpgaAutoReconfigureEnabled_ExpectAndReturn( true );
    BSP_FpgaReconfigure_ExpectAndReturn( false );
    poll_at( 3000 );

    TEST_ASSERT_EQUAL_UINT32_MESSAGE( 3u << 19, MOCK_BSP_WatchdogSnapshot( 2 ) & ( 0x7fu << 19 ),
                                      field_name );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE( 0, MOCK_BSP_WatchdogSnapshot( 2 ) & ( 0x3fu << 26 ),
                                      field_name );
}
