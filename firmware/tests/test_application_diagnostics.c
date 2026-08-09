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


void test_usb_free_heartbeat_keeps_reporting_the_boot_reason_while_it_runs( void )
{
    start_led_only( BSP_BOOT_WATCHDOG, APPLICATION_DIAGNOSTICS_MARKER_LOOP, 255, 0, 0, 1, 255, 0,
                    0 );

    BSP_LedOff_Expect();
    poll_at( 250 );
    BSP_LedSet_Expect( 255, 0, 0, BRIGHTNESS );
    poll_at( 500 );

    /* the stub reports no host, so the one-second sample must not repaint it blue */
    BSP_LedOff_Expect();
    poll_at( 750 );
    BSP_LedSet_Expect( 255, 0, 0, BRIGHTNESS );
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_LedGet_ExpectAndReturn( led_state( 255, 0, 0, true ) );
    poll_at( 1000 );
}


void test_poll_feeds_the_watchdog_and_marks_the_loop_before_any_deadline( void )
{
    start_usb_at( 0 );

    poll_at( 10 );

    TEST_ASSERT_EQUAL_UINT32( 1, MOCK_BSP_WatchdogFeedCount() );
    TEST_ASSERT_EQUAL_UINT32( APPLICATION_DIAGNOSTICS_MARKER_LOOP, MOCK_BSP_WatchdogMarker() );
}


void test_led_heartbeat_toggles_at_two_hertz( void )
{
    start_usb_at( 0 );

    poll_at( 249 );
    BSP_LedOff_Expect();
    poll_at( 250 );
    BSP_LedSet_Expect( 0, 0, 255, BRIGHTNESS );
    poll_at( 500 );
    BSP_LedOff_Expect();
    poll_at( 750 );

    TEST_ASSERT_EQUAL_UINT32( 4, MOCK_BSP_WatchdogFeedCount() );
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


/* The heartbeat writes every 250 ms, which is faster than anything a person can
   watch. A light show or an LED test that holds a colour for longer than that
   gets the heartbeat punched through the middle of it, so an activity takes the
   LED for its whole run rather than sharing it. */
void test_released_led_is_left_alone_by_the_heartbeat( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    application_diagnostics_release_led();

    /* Two full heartbeat periods and a sample boundary, and not one LED write.
       Any BSP_LedSet or BSP_LedOff here would fail as an unexpected call. */
    poll_at( 500 );
    poll_at( 750 );
    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    poll_at( 1000 );
}


/* The readback is only evidence while the heartbeat owns the LED. Comparing it
   against a command that is no longer being issued would report an FPGA fault
   once a second for the length of every light show -- which is the trap in
   simply muting the heartbeat and calling it done. */
void test_released_led_is_dropped_from_the_fpga_health_check( void )
{
    start_usb_at( 0 );
    application_diagnostics_release_led();

    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    poll_at( 1000 );

    application_diagnostics_print_report();
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "fpga_fail=0" ) );
}


/* Reclaiming writes immediately rather than waiting for the next 250 ms edge, so
   the last owner's colour does not linger and the health check never samples
   against a command that predates the handover. */
void test_reclaiming_the_led_repaints_it_at_once( void )
{
    start_usb_at( 0 );
    application_diagnostics_release_led();

    BSP_LedSet_Expect( 0, 0, 255, BRIGHTNESS );
    application_diagnostics_reclaim_led();

    /* And the heartbeat carries on from the phase it would have been in, rather
       than restarting, so handing the LED back does not stretch or shorten the
       beat that a watching eye is using to judge that the loop is alive. */
    BSP_LedOff_Expect();
    poll_at( 250 );
    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 1000 );
}


void test_heartbeat_stays_blue_while_the_host_has_not_asserted_dtr( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    MOCK_BSP_UsbSetHealth( health_of( false, false, 64, 5, 0, 100 ) );
    expect_sample( 0, 0, 255 );
    poll_at( 1000 );

    TEST_ASSERT_EQUAL_UINT32( 100u, MOCK_BSP_WatchdogSnapshot( 2 ) );
}


void test_heartbeat_turns_magenta_while_the_bus_is_suspended( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    MOCK_BSP_UsbSetHealth( health_of( true, true, 64, 5, 0, 100 ) );
    expect_sample( 255, 0, 255 );
    poll_at( 1000 );

    TEST_ASSERT_EQUAL_UINT32( 100u | ( 1u << 16 ) | ( 1u << 17 ), MOCK_BSP_WatchdogSnapshot( 2 ) );
}


void test_heartbeat_turns_magenta_when_the_frame_counter_freezes( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );

    /* transfers still complete, but the host has stopped sending start-of-frame */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 6, 0, 100 ) );
    expect_sample( 255, 0, 255 );
    poll_at( 6000 );
}


void test_heartbeat_turns_red_when_a_full_fifo_stops_draining( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );

    /* transmit FIFO full and no outbound completion: the stall run and its
       epoch start at this sample, not at the last sample that had room */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 101 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 2000 );

    BSP_LedOff_Expect();
    poll_at( 2250 );

    /* a genuine endpoint wedge: the whole window elapsed from the first full sample */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 102 ) );
    expect_sample( 255, 0, 0 );
    poll_at( 2000 + APPLICATION_DIAGNOSTICS_FIFO_STALL_MS );

    TEST_ASSERT_EQUAL_UINT32( 102u | ( 1u << 16 ) | ( 1u << 18 ), MOCK_BSP_WatchdogSnapshot( 2 ) );
}


/* A momentarily full FIFO is normal under load; it only means anything if the
   transmit path also stops completing transfers. */
void test_a_full_fifo_that_is_still_draining_stays_green( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    /* full, but an outbound completion since the last sample proves movement */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 7, 100 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 1000 );

    TEST_ASSERT_EQUAL_UINT32( 100u | ( 1u << 16 ) | ( 1u << 18 ), MOCK_BSP_WatchdogSnapshot( 2 ) );
}


/* Regression guard. A quiet link is not a fault. Keying red on the traffic gap
   alone made the heartbeat alternate green and red against the firmware's own
   10 s idle-status cadence, reporting an endpoint wedge on every cycle. */
void test_a_quiet_link_with_room_in_the_fifo_stays_green( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );

    /* far beyond the activity threshold, but the FIFO has room, so nothing is
       stuck; the frame counter keeps advancing so the host is still framing */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 101 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 1000 + APPLICATION_DIAGNOSTICS_FIFO_STALL_MS + 5000 );
}


/* Regression guard for the other half of the same fault: quiet history must not
   count against a FIFO that only just filled. Measuring the stall from the last
   CDC traffic made a link idle longer than the threshold go red on the first
   full sample, with no wedge ever having lasted a single second. */
void test_a_fifo_that_fills_after_a_quiet_spell_is_not_immediately_red( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );

    /* a long quiet spell: no CDC traffic since t=1000, but the FIFO has room */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 135 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 35000 );

    BSP_LedOff_Expect();
    poll_at( 35250 );

    /* the FIFO fills one second later; the stall clock starts here, not 35 s ago */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 136 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 36000 );
}


/* The threshold is measured from the first sample that saw the FIFO full with
   no TX progress -- t=2000 here, not the t=1000 sample that still had room.
   Anchoring on the last room sample fired after only 29 s of observed
   fullness; this pins green at epoch+29 s and red exactly at epoch+30 s. */
void test_red_requires_the_fifo_to_stay_full_for_the_whole_stall_window( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );

    /* first sample observing the FIFO full with no TX progress: the epoch */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 101 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 2000 );

    BSP_LedOff_Expect();
    poll_at( 2250 );

    /* one second short of the window measured from the epoch: still green */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 102 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 2000 + APPLICATION_DIAGNOSTICS_FIFO_STALL_MS - 1000 );

    BSP_LedOff_Expect();
    poll_at( 2000 + APPLICATION_DIAGNOSTICS_FIFO_STALL_MS - 750 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 103 ) );
    expect_sample( 255, 0, 0 );
    poll_at( 2000 + APPLICATION_DIAGNOSTICS_FIFO_STALL_MS );
}


void test_a_draining_fifo_restarts_the_stall_measurement( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    /* full with no progress for 16 s -- more than half the window */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 100 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );

    /* room appears: whatever was queued went out, so the run is broken */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 117 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 17000 );

    BSP_LedOff_Expect();
    poll_at( 17250 );

    /* full again: a fresh epoch starts at this sample */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 118 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 18000 );

    BSP_LedOff_Expect();
    poll_at( 18250 );

    /* 29 s from the fresh epoch: green, because the earlier 16 s of fullness
       must not be carried over into the new window */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 147 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 18000 + APPLICATION_DIAGNOSTICS_FIFO_STALL_MS - 1000 );

    BSP_LedOff_Expect();
    poll_at( 18000 + APPLICATION_DIAGNOSTICS_FIFO_STALL_MS - 750 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 148 ) );
    expect_sample( 255, 0, 0 );
    poll_at( 18000 + APPLICATION_DIAGNOSTICS_FIFO_STALL_MS );
}


/* The regression this change exists for: only outbound completions may clear
   the stall run. The pooled activity counter used to let a chatty host typing
   into a wedged device keep the lamp green forever. */
void test_rx_only_activity_cannot_conceal_a_stalled_tx_fifo( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    /* full with no TX completion: the epoch starts here */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 100 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );

    /* inbound traffic keeps the pooled counter moving; the TX counter does not */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 20, 0, 115 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 16000 );

    BSP_LedOff_Expect();
    poll_at( 16250 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 40, 0, 130 ) );
    expect_sample( 255, 0, 0 );
    poll_at( 1000 + APPLICATION_DIAGNOSTICS_FIFO_STALL_MS );
}


void test_tx_completions_restart_the_stall_interval( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    /* full, but outbound transfers keep completing: never a stall run */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 7, 100 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );

    /* far beyond the window, and still green -- data demonstrably moved */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 8, 134 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 34000 );

    BSP_LedOff_Expect();
    poll_at( 34250 );

    /* the completions stop: a fresh epoch starts at this sample */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 8, 135 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 35000 );

    BSP_LedOff_Expect();
    poll_at( 35250 );

    /* one second short of the window from the fresh epoch: green */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 8, 164 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 35000 + APPLICATION_DIAGNOSTICS_FIFO_STALL_MS - 1000 );

    BSP_LedOff_Expect();
    poll_at( 35000 + APPLICATION_DIAGNOSTICS_FIFO_STALL_MS - 750 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 8, 165 ) );
    expect_sample( 255, 0, 0 );
    poll_at( 35000 + APPLICATION_DIAGNOSTICS_FIFO_STALL_MS );
}


/* An ordered comparison would read the counter wrapping past zero as "no
   progress" and hold a stale epoch; the != comparison must treat it as
   movement like any other change. */
void test_tx_counter_wraparound_still_counts_as_progress( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 250 );

    /* park the tracked TX count at the top of its range */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0xffffffffu, 100 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 1000 );

    BSP_LedOff_Expect();
    poll_at( 1250 );

    /* counter unmoved: a stall run begins here */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0xffffffffu, 101 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 2000 );

    BSP_LedOff_Expect();
    poll_at( 2250 );

    /* the counter wraps to zero: progress, so the run breaks */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 115 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 16000 );

    BSP_LedOff_Expect();
    poll_at( 16250 );

    /* unmoved again: a fresh epoch at t=17000. Green at 46000 proves the wrap
       reset the measurement -- a broken ordered compare would still be holding
       the t=2000 epoch and have gone red at 32000. */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 116 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 17000 );

    BSP_LedOff_Expect();
    poll_at( 17250 );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 145 ) );
    expect_sample( 0, 255, 0 );
    poll_at( 17000 + APPLICATION_DIAGNOSTICS_FIFO_STALL_MS - 1000 );
}


/* Pins the wrap-safe signed-difference idiom: the stall window here spans the
   32-bit millisecond rollover, and an unsigned comparison would either fire
   early or park the verdict for 49 days. */
void test_fifo_stall_measurement_survives_the_millisecond_wrap( void )
{
    const uint32_t start_ms = 0xffff8000u;

    start_usb_at( start_ms );
    BSP_LedOff_Expect();
    poll_at( start_ms + 250u );

    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    expect_sample( 0, 255, 0 );
    poll_at( start_ms + 1000u );

    BSP_LedOff_Expect();
    poll_at( start_ms + 1250u );

    /* full from here; the epoch is this sample at start+2000, before the wrap */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 101 ) );
    expect_sample( 0, 255, 0 );
    poll_at( start_ms + 2000u );

    BSP_LedOff_Expect();
    poll_at( start_ms + 2250u );

    /* 33 s after the epoch, at a timestamp that has wrapped past zero: the
       verdict must still fire */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 0, 5, 0, 102 ) );
    expect_sample( 255, 0, 0 );
    poll_at( start_ms + 35000u );
}


void test_a_sample_without_a_heartbeat_toggle_still_refreshes_the_led( void )
{
    start_usb_at( 0 );
    BSP_LedOff_Expect();
    poll_at( 999 );

    /* 999 moved the heartbeat deadline to 1249, so only the sample is due here */
    MOCK_BSP_UsbSetHealth( health_of( true, false, 64, 5, 0, 100 ) );
    BSP_LedOff_Expect();
    BSP_FpgaCdone_ExpectAndReturn( true );
    BSP_FpgaPing_ExpectAndReturn( BSP_FPGA_DESIGN_ID );
    BSP_LedGet_ExpectAndReturn( led_state( 0, 0, 255, false ) );
    poll_at( 1000 );

    TEST_ASSERT_EQUAL_UINT32( 1, MOCK_BSP_WatchdogSnapshot( 0 ) );
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
