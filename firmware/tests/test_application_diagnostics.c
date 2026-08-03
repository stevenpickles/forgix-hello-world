#include "unity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "application_diagnostics.h"
#include "mock_bsp_console.h"
#include "mock_bsp_time.h"
#include "mock_bsp_usb.h"
#include "mock_bsp_watchdog.h"
#include "mock_auto_bsp_fpga.h"
#include "mock_auto_bsp_led.h"

enum { BRIGHTNESS = 64 };

void setUp(void) {
    MOCK_BSP_ConsoleReset();
    MOCK_BSP_TimeReset();
    MOCK_BSP_UsbReset();
    MOCK_BSP_WatchdogReset();
}

void tearDown(void) {
}

static bsp_led_state_t led_state(uint8_t red, uint8_t green, uint8_t blue, bool enabled) {
    bsp_led_state_t state = {
        .red = red,
        .green = green,
        .blue = blue,
        .brightness = BRIGHTNESS,
        .enabled = enabled,
    };
    return state;
}

static bsp_usb_health_t health_of(bool connected, bool suspended, uint32_t write_available,
                                  uint32_t activity_count, uint32_t frame_number) {
    bsp_usb_health_t health = {
        .connected = connected,
        .suspended = suspended,
        .write_available = write_available,
        .activity_count = activity_count,
        .frame_number = frame_number,
    };
    return health;
}

/* The heartbeat starts blue in both images: health has not been sampled yet, so
   the host counts as absent. */
static void start_usb_at(uint32_t now_ms) {
    MOCK_BSP_UsbSetPresent(true);
    MOCK_BSP_TimeSetMs(now_ms);
    BSP_LedSet_Expect(0, 0, 255, BRIGHTNESS);
    application_diagnostics_start();
}

/* The blink code repeats, because it cannot be replayed: a power cycle to watch
   it again would reset the scratch registers it is reporting. */
enum { BOOT_REPEATS = 3 };

static void start_led_only(bsp_boot_reason reason, uint32_t marker, uint8_t red, uint8_t green,
                           uint8_t blue, uint32_t blinks, uint8_t rest_red, uint8_t rest_green,
                           uint8_t rest_blue) {
    MOCK_BSP_UsbSetPresent(false);
    MOCK_BSP_WatchdogSetBootReason(reason);
    MOCK_BSP_WatchdogSetRetained(marker, 0, 0, 0);
    MOCK_BSP_TimeSetMs(0);

    BSP_LedOff_Expect();
    for (uint32_t pass = 0; pass < BOOT_REPEATS; ++pass) {
        for (uint32_t blink = 0; blink < blinks; ++blink) {
            BSP_LedSet_Expect(red, green, blue, BRIGHTNESS);
            BSP_LedOff_Expect();
        }
    }
    BSP_LedSet_Expect(rest_red, rest_green, rest_blue, BRIGHTNESS);
    application_diagnostics_start();
}

static void poll_at(uint32_t now_ms) {
    MOCK_BSP_TimeSetMs(now_ms);
    application_diagnostics_poll();
}

/* Advances to the first one-second sample with the supplied health, leaving the
   heartbeat lit so the color under test is observable. */
static void expect_sample(uint8_t red, uint8_t green, uint8_t blue) {
    BSP_LedSet_Expect(red, green, blue, BRIGHTNESS);
    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_LedGet_ExpectAndReturn(led_state(red, green, blue, true));
}

void test_start_reports_the_retained_watchdog_evidence_and_arms_the_watchdog(void) {
    MOCK_BSP_WatchdogSetBootReason(BSP_BOOT_WATCHDOG);
    MOCK_BSP_WatchdogSetRetained(APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE, 612, 44,
                                   0x00010001);

    start_usb_at(0);

    TEST_ASSERT_EQUAL_STRING("diag: boot=watchdog marker=3 loop=612 usb=44 health=00010001\n",
                             MOCK_BSP_ConsoleOutput());
    TEST_ASSERT_TRUE(MOCK_BSP_WatchdogStarted());
    TEST_ASSERT_EQUAL_UINT32(APPLICATION_DIAGNOSTICS_WATCHDOG_TIMEOUT_MS,
                             MOCK_BSP_WatchdogTimeoutMs());
    TEST_ASSERT_EQUAL_UINT32(APPLICATION_DIAGNOSTICS_MARKER_LOOP, MOCK_BSP_WatchdogMarker());
    for (uint32_t slot = 0; slot < BSP_WATCHDOG_SNAPSHOT_SLOTS; ++slot) {
        TEST_ASSERT_EQUAL_UINT32(0, MOCK_BSP_WatchdogSnapshot(slot));
    }
}

void test_start_names_every_boot_reason_on_the_console(void) {
    const struct {
        bsp_boot_reason reason;
        const char *name;
    } cases[] = {
        {BSP_BOOT_POWER_ON, "boot=power-on"},
        {BSP_BOOT_BROWNOUT, "boot=brownout"},
        {BSP_BOOT_WATCHDOG, "boot=watchdog"},
        {BSP_BOOT_OTHER, "boot=other"},
    };

    for (uint32_t index = 0; index < (uint32_t)(sizeof cases / sizeof cases[0]); ++index) {
        MOCK_BSP_ConsoleReset();
        MOCK_BSP_WatchdogReset();
        MOCK_BSP_WatchdogSetBootReason(cases[index].reason);

        start_usb_at(0);

        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(MOCK_BSP_ConsoleOutput(), cases[index].name),
                                     cases[index].name);
    }
}

/* The snapshot is taken before the watchdog is armed and has to stay readable
   afterwards. Arming writes the scratch word watchdog_enable_caused_reboot
   consults, so anything that asks the BSP again later is told every board came
   back from a watchdog reset -- which is precisely what the built-in test
   reported on hardware that had powered up cleanly. */
void test_boot_reason_is_published_from_the_snapshot_taken_before_the_watchdog_was_armed(void) {
    start_led_only(BSP_BOOT_BROWNOUT, 0, 255, 255, 0, 2, 255, 255, 0);

    TEST_ASSERT_TRUE(MOCK_BSP_WatchdogStarted());
    TEST_ASSERT_EQUAL_INT(BSP_BOOT_BROWNOUT, application_diagnostics_boot_reason());
}

void test_usb_free_image_blinks_a_white_power_on_code_and_still_prints_it(void) {
    start_led_only(BSP_BOOT_POWER_ON, 0, 255, 255, 255, 1, 0, 0, 255);

    /* The print costs nothing with no stdio backend linked, and carries the
       report over UART when the image is built with FORGIX_DIAGNOSTIC_UART. */
    TEST_ASSERT_EQUAL_STRING("diag: boot=power-on marker=0 loop=0 usb=0 health=00000000\n",
                             MOCK_BSP_ConsoleOutput());
    /* one leading gap, then per pass: an on/off pair per blink plus a gap */
    TEST_ASSERT_EQUAL_UINT32(1 + BOOT_REPEATS * (1 * 2 + 1), MOCK_BSP_TimeSleepCount());
    TEST_ASSERT_TRUE(MOCK_BSP_WatchdogStarted());
}

/* The per-second line is the MCU-liveness proof the LED cannot give: it depends
   only on the foreground loop, so the last logged second dates a freeze exactly. */
void test_usb_free_image_logs_a_line_every_second(void) {
    start_led_only(BSP_BOOT_POWER_ON, 0, 255, 255, 255, 1, 0, 0, 255);
    BSP_LedOff_Expect();
    poll_at(250);
    MOCK_BSP_ConsoleReset();

    BSP_LedSet_Expect(0, 0, 255, BRIGHTNESS);
    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_LedGet_ExpectAndReturn(led_state(0, 0, 255, true));
    poll_at(1000);

    TEST_ASSERT_EQUAL_STRING("diag: t=1s led=1 fpga_fail=0 fpga_reconfig=0 marker=6\n",
                             MOCK_BSP_ConsoleOutput());
}

void test_recovery_is_skipped_when_auto_reconfigure_is_disabled(void) {
    start_usb_at(0);
    BSP_LedOff_Expect();
    poll_at(250);

    MOCK_BSP_UsbSetHealth(health_of(true, false, 64, 5, 100));
    BSP_LedSet_Expect(0, 255, 0, BRIGHTNESS);
    BSP_FpgaCdone_ExpectAndReturn(false);
    BSP_FpgaAutoReconfigureEnabled_ExpectAndReturn(false);
    poll_at(1000);

    /* the fault is still counted, so a run records it without disturbing it */
    TEST_ASSERT_EQUAL_UINT32(1u << 19, MOCK_BSP_WatchdogSnapshot(2) & (0x7fu << 19));
    TEST_ASSERT_EQUAL_UINT32(0, MOCK_BSP_WatchdogSnapshot(2) & (0x3fu << 26));
}

/* Each boot reason keeps its resting heartbeat color for the whole run, so the
   verdict is readable hours later without having caught the blink code. */
void test_usb_free_image_blinks_brownout_in_yellow_and_rests_yellow(void) {
    start_led_only(BSP_BOOT_BROWNOUT, 0, 255, 255, 0, 2, 255, 255, 0);
}

void test_usb_free_image_blinks_unclassified_resets_in_cyan_and_rests_cyan(void) {
    start_led_only(BSP_BOOT_OTHER, 0, 0, 255, 255, 3, 0, 255, 255);
}

void test_usb_free_image_blinks_the_retained_marker_in_red_and_rests_red(void) {
    start_led_only(BSP_BOOT_WATCHDOG, APPLICATION_DIAGNOSTICS_MARKER_COMMAND, 255, 0, 0, 4, 255, 0,
                   0);
}

void test_watchdog_blink_count_is_clamped_to_a_readable_range(void) {
    start_led_only(BSP_BOOT_WATCHDOG, 0, 255, 0, 0, 1, 255, 0, 0);
    start_led_only(BSP_BOOT_WATCHDOG, 20, 255, 0, 0, 8, 255, 0, 0);
}

void test_usb_free_heartbeat_keeps_reporting_the_boot_reason_while_it_runs(void) {
    start_led_only(BSP_BOOT_WATCHDOG, APPLICATION_DIAGNOSTICS_MARKER_LOOP, 255, 0, 0, 1, 255, 0, 0);

    BSP_LedOff_Expect();
    poll_at(250);
    BSP_LedSet_Expect(255, 0, 0, BRIGHTNESS);
    poll_at(500);

    /* the stub reports no host, so the one-second sample must not repaint it blue */
    BSP_LedOff_Expect();
    poll_at(750);
    BSP_LedSet_Expect(255, 0, 0, BRIGHTNESS);
    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_LedGet_ExpectAndReturn(led_state(255, 0, 0, true));
    poll_at(1000);
}

void test_poll_feeds_the_watchdog_and_marks_the_loop_before_any_deadline(void) {
    start_usb_at(0);

    poll_at(10);

    TEST_ASSERT_EQUAL_UINT32(1, MOCK_BSP_WatchdogFeedCount());
    TEST_ASSERT_EQUAL_UINT32(APPLICATION_DIAGNOSTICS_MARKER_LOOP, MOCK_BSP_WatchdogMarker());
}

void test_led_heartbeat_toggles_at_two_hertz(void) {
    start_usb_at(0);

    poll_at(249);
    BSP_LedOff_Expect();
    poll_at(250);
    BSP_LedSet_Expect(0, 0, 255, BRIGHTNESS);
    poll_at(500);
    BSP_LedOff_Expect();
    poll_at(750);

    TEST_ASSERT_EQUAL_UINT32(4, MOCK_BSP_WatchdogFeedCount());
}

void test_sample_shows_green_and_snapshots_health_while_traffic_advances(void) {
    start_usb_at(0);
    BSP_LedOff_Expect();
    poll_at(250);

    MOCK_BSP_UsbSetHealth(health_of(true, false, 64, 5, 100));
    expect_sample(0, 255, 0);
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32(1, MOCK_BSP_WatchdogSnapshot(0));
    TEST_ASSERT_EQUAL_UINT32(5, MOCK_BSP_WatchdogSnapshot(1));
    TEST_ASSERT_EQUAL_UINT32(100u | (1u << 16), MOCK_BSP_WatchdogSnapshot(2));
    TEST_ASSERT_TRUE(
        MOCK_BSP_WatchdogMarkerWasWritten(APPLICATION_DIAGNOSTICS_MARKER_USB_SNAPSHOT));
    TEST_ASSERT_TRUE(
        MOCK_BSP_WatchdogMarkerWasWritten(APPLICATION_DIAGNOSTICS_MARKER_FPGA_CHECK));
    TEST_ASSERT_EQUAL_UINT32(APPLICATION_DIAGNOSTICS_MARKER_LOOP, MOCK_BSP_WatchdogMarker());
}

void test_heartbeat_stays_blue_while_the_host_has_not_asserted_dtr(void) {
    start_usb_at(0);
    BSP_LedOff_Expect();
    poll_at(250);

    MOCK_BSP_UsbSetHealth(health_of(false, false, 64, 5, 100));
    expect_sample(0, 0, 255);
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32(100u, MOCK_BSP_WatchdogSnapshot(2));
}

void test_heartbeat_turns_magenta_while_the_bus_is_suspended(void) {
    start_usb_at(0);
    BSP_LedOff_Expect();
    poll_at(250);

    MOCK_BSP_UsbSetHealth(health_of(true, true, 64, 5, 100));
    expect_sample(255, 0, 255);
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32(100u | (1u << 16) | (1u << 17), MOCK_BSP_WatchdogSnapshot(2));
}

void test_heartbeat_turns_magenta_when_the_frame_counter_freezes(void) {
    start_usb_at(0);
    BSP_LedOff_Expect();
    poll_at(250);

    MOCK_BSP_UsbSetHealth(health_of(true, false, 64, 5, 100));
    expect_sample(0, 255, 0);
    poll_at(1000);

    BSP_LedOff_Expect();
    poll_at(1250);

    /* transfers still complete, but the host has stopped sending start-of-frame */
    MOCK_BSP_UsbSetHealth(health_of(true, false, 64, 6, 100));
    expect_sample(255, 0, 255);
    poll_at(6000);
}

void test_heartbeat_turns_red_when_a_full_fifo_stops_draining(void) {
    start_usb_at(0);
    BSP_LedOff_Expect();
    poll_at(250);

    MOCK_BSP_UsbSetHealth(health_of(true, false, 64, 5, 100));
    expect_sample(0, 255, 0);
    poll_at(1000);

    BSP_LedOff_Expect();
    poll_at(1250);

    /* transmit FIFO full and nothing completing: a genuine endpoint wedge */
    MOCK_BSP_UsbSetHealth(health_of(true, false, 0, 5, 101));
    expect_sample(255, 0, 0);
    poll_at(1000 + APPLICATION_DIAGNOSTICS_ACTIVITY_STALL_MS);

    TEST_ASSERT_EQUAL_UINT32(101u | (1u << 16) | (1u << 18), MOCK_BSP_WatchdogSnapshot(2));
}

/* A momentarily full FIFO is normal under load; it only means anything if it
   also stops draining. */
void test_a_full_fifo_that_is_still_draining_stays_green(void) {
    start_usb_at(0);
    BSP_LedOff_Expect();
    poll_at(250);

    MOCK_BSP_UsbSetHealth(health_of(true, false, 0, 5, 100));
    expect_sample(0, 255, 0);
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32(100u | (1u << 16) | (1u << 18), MOCK_BSP_WatchdogSnapshot(2));
}

/* Regression guard. A quiet link is not a fault. Keying red on the traffic gap
   alone made the heartbeat alternate green and red against the firmware's own
   10 s idle-status cadence, reporting an endpoint wedge on every cycle. */
void test_a_quiet_link_with_room_in_the_fifo_stays_green(void) {
    start_usb_at(0);
    BSP_LedOff_Expect();
    poll_at(250);

    MOCK_BSP_UsbSetHealth(health_of(true, false, 64, 5, 100));
    expect_sample(0, 255, 0);
    poll_at(1000);

    BSP_LedOff_Expect();
    poll_at(1250);

    /* far beyond the activity threshold, but the FIFO has room, so nothing is
       stuck; the frame counter keeps advancing so the host is still framing */
    MOCK_BSP_UsbSetHealth(health_of(true, false, 64, 5, 101));
    expect_sample(0, 255, 0);
    poll_at(1000 + APPLICATION_DIAGNOSTICS_ACTIVITY_STALL_MS + 5000);
}

void test_a_sample_without_a_heartbeat_toggle_still_refreshes_the_led(void) {
    start_usb_at(0);
    BSP_LedOff_Expect();
    poll_at(999);

    /* 999 moved the heartbeat deadline to 1249, so only the sample is due here */
    MOCK_BSP_UsbSetHealth(health_of(true, false, 64, 5, 100));
    BSP_LedOff_Expect();
    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_LedGet_ExpectAndReturn(led_state(0, 0, 255, false));
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32(1, MOCK_BSP_WatchdogSnapshot(0));
}

void test_lost_configuration_reconfigures_and_flies_the_recovery_signature(void) {
    start_usb_at(0);
    BSP_LedOff_Expect();
    poll_at(250);

    MOCK_BSP_UsbSetHealth(health_of(true, false, 64, 5, 100));
    BSP_LedSet_Expect(0, 255, 0, BRIGHTNESS);
    BSP_FpgaCdone_ExpectAndReturn(false);
    BSP_FpgaAutoReconfigureEnabled_ExpectAndReturn(true);
    BSP_FpgaReconfigure_ExpectAndReturn(true);
    BSP_LedSet_Expect(255, 255, 255, BRIGHTNESS);
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32(100u | (1u << 16) | (1u << 19) | (1u << 26),
                             MOCK_BSP_WatchdogSnapshot(2));

    /* the signature survives the following toggles, then healthy color resumes */
    BSP_LedOff_Expect();
    poll_at(1250);
    BSP_LedSet_Expect(255, 255, 255, BRIGHTNESS);
    poll_at(1500);
    BSP_LedOff_Expect();
    poll_at(1750);
    BSP_LedSet_Expect(255, 255, 255, BRIGHTNESS);
    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_LedGet_ExpectAndReturn(led_state(255, 255, 255, true));
    poll_at(2000);
    BSP_LedOff_Expect();
    poll_at(2250);
    BSP_LedSet_Expect(255, 255, 255, BRIGHTNESS);
    poll_at(2500);
    BSP_LedOff_Expect();
    poll_at(2750);
    expect_sample(0, 255, 0);
    poll_at(3000);
}

void test_a_wrong_design_id_reconfigures_without_reading_the_led_back(void) {
    start_usb_at(0);
    BSP_LedOff_Expect();
    poll_at(250);

    MOCK_BSP_UsbSetHealth(health_of(true, false, 64, 5, 100));
    BSP_LedSet_Expect(0, 255, 0, BRIGHTNESS);
    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(0x00);
    BSP_FpgaAutoReconfigureEnabled_ExpectAndReturn(true);
    BSP_FpgaReconfigure_ExpectAndReturn(true);
    BSP_LedSet_Expect(255, 255, 255, BRIGHTNESS);
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32((1u << 19) | (1u << 26),
                             MOCK_BSP_WatchdogSnapshot(2) & ((0x7fu << 19) | (0x3fu << 26)));
}

/* A failed reconfiguration must leave the failure counted but fly no recovery
   signature, so the operator can tell "self-healed" from "still broken". */
static void run_readback_mismatch(bsp_led_state_t readback, const char *field_name) {
    MOCK_BSP_ConsoleReset();
    MOCK_BSP_TimeReset();
    MOCK_BSP_UsbReset();
    MOCK_BSP_WatchdogReset();

    start_usb_at(0);
    BSP_LedOff_Expect();
    poll_at(250);

    MOCK_BSP_UsbSetHealth(health_of(true, false, 64, 5, 100));
    BSP_LedSet_Expect(0, 255, 0, BRIGHTNESS);
    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_LedGet_ExpectAndReturn(readback);
    BSP_FpgaAutoReconfigureEnabled_ExpectAndReturn(true);
    BSP_FpgaReconfigure_ExpectAndReturn(false);
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u << 19, MOCK_BSP_WatchdogSnapshot(2) & (0x7fu << 19),
                                     field_name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, MOCK_BSP_WatchdogSnapshot(2) & (0x3fu << 26), field_name);
}

void test_each_led_register_readback_mismatch_is_treated_as_an_fpga_fault(void) {
    const char *field_names[] = {"red", "green", "blue", "brightness", "enabled"};
    bsp_led_state_t mismatches[5];

    for (uint32_t index = 0; index < 5u; ++index) {
        mismatches[index] = led_state(0, 255, 0, true);
    }
    mismatches[0].red = 1;
    mismatches[1].green = 254;
    mismatches[2].blue = 1;
    mismatches[3].brightness = BRIGHTNESS - 1;
    mismatches[4].enabled = false;

    for (uint32_t index = 0; index < 5u; ++index) {
        run_readback_mismatch(mismatches[index], field_names[index]);
    }
}

void test_diag_report_lists_the_previous_boot_and_the_live_counters(void) {
    MOCK_BSP_WatchdogSetBootReason(BSP_BOOT_BROWNOUT);
    MOCK_BSP_WatchdogSetRetained(2, 7, 8, 9);
    start_usb_at(0);
    BSP_LedOff_Expect();
    poll_at(250);
    MOCK_BSP_UsbSetHealth(health_of(true, false, 32, 5, 100));
    expect_sample(0, 255, 0);
    poll_at(1000);
    MOCK_BSP_ConsoleReset();

    application_diagnostics_print_report();

    TEST_ASSERT_EQUAL_STRING(
        "diag: boot=brownout marker=2 loop=7 usb=8 health=00000009\n"
        "diag: uptime=1s connected=1 suspended=0 write=32 activity=5 sof=100 "
        "fpga_fail=0 fpga_reconfig=0\n",
        MOCK_BSP_ConsoleOutput());
}
