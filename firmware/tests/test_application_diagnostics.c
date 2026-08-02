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
    mock_bsp_console_reset();
    mock_bsp_time_reset();
    mock_bsp_usb_reset();
    mock_bsp_watchdog_reset();
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
    mock_bsp_usb_set_present(true);
    mock_bsp_time_set_ms(now_ms);
    bsp_led_set_Expect(0, 0, 255, BRIGHTNESS);
    application_diagnostics_start();
}

static void start_led_only(bsp_boot_reason_t reason, uint32_t marker, uint8_t red, uint8_t green,
                           uint8_t blue, uint32_t blinks) {
    mock_bsp_usb_set_present(false);
    mock_bsp_watchdog_set_boot_reason(reason);
    mock_bsp_watchdog_set_retained(marker, 0, 0, 0);
    mock_bsp_time_set_ms(0);

    bsp_led_off_Expect();
    for (uint32_t blink = 0; blink < blinks; ++blink) {
        bsp_led_set_Expect(red, green, blue, BRIGHTNESS);
        bsp_led_off_Expect();
    }
    bsp_led_set_Expect(0, 0, 255, BRIGHTNESS);
    application_diagnostics_start();
}

static void poll_at(uint32_t now_ms) {
    mock_bsp_time_set_ms(now_ms);
    application_diagnostics_poll();
}

/* Advances to the first one-second sample with the supplied health, leaving the
   heartbeat lit so the color under test is observable. */
static void expect_sample(uint8_t red, uint8_t green, uint8_t blue) {
    bsp_led_set_Expect(red, green, blue, BRIGHTNESS);
    bsp_fpga_cdone_ExpectAndReturn(true);
    bsp_fpga_ping_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    bsp_led_get_ExpectAndReturn(led_state(red, green, blue, true));
}

void test_start_reports_the_retained_watchdog_evidence_and_arms_the_watchdog(void) {
    mock_bsp_watchdog_set_boot_reason(BSP_BOOT_WATCHDOG);
    mock_bsp_watchdog_set_retained(APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE, 612, 44,
                                   0x00010001);

    start_usb_at(0);

    TEST_ASSERT_EQUAL_STRING("diag: boot=watchdog marker=3 loop=612 usb=44 health=00010001\n",
                             mock_bsp_console_output());
    TEST_ASSERT_TRUE(mock_bsp_watchdog_started());
    TEST_ASSERT_EQUAL_UINT32(APPLICATION_DIAGNOSTICS_WATCHDOG_TIMEOUT_MS,
                             mock_bsp_watchdog_timeout_ms());
    TEST_ASSERT_EQUAL_UINT32(APPLICATION_DIAGNOSTICS_MARKER_LOOP, mock_bsp_watchdog_marker());
    for (uint32_t slot = 0; slot < BSP_WATCHDOG_SNAPSHOT_SLOTS; ++slot) {
        TEST_ASSERT_EQUAL_UINT32(0, mock_bsp_watchdog_snapshot(slot));
    }
}

void test_start_names_every_boot_reason_on_the_console(void) {
    const struct {
        bsp_boot_reason_t reason;
        const char *name;
    } cases[] = {
        {BSP_BOOT_POWER_ON, "boot=power-on"},
        {BSP_BOOT_BROWNOUT, "boot=brownout"},
        {BSP_BOOT_WATCHDOG, "boot=watchdog"},
        {BSP_BOOT_OTHER, "boot=other"},
    };

    for (size_t index = 0; index < sizeof cases / sizeof cases[0]; ++index) {
        mock_bsp_console_reset();
        mock_bsp_watchdog_reset();
        mock_bsp_watchdog_set_boot_reason(cases[index].reason);

        start_usb_at(0);

        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mock_bsp_console_output(), cases[index].name),
                                     cases[index].name);
    }
}

void test_usb_free_image_blinks_a_white_power_on_code_instead_of_printing(void) {
    start_led_only(BSP_BOOT_POWER_ON, 0, 255, 255, 255, 1);

    TEST_ASSERT_EQUAL_STRING("", mock_bsp_console_output());
    /* one leading gap, one on/off pair per blink, one trailing gap */
    TEST_ASSERT_EQUAL_UINT32(4, mock_bsp_time_sleep_count());
    TEST_ASSERT_TRUE(mock_bsp_watchdog_started());
}

void test_usb_free_image_blinks_brownout_in_yellow(void) {
    start_led_only(BSP_BOOT_BROWNOUT, 0, 255, 255, 0, 2);
}

void test_usb_free_image_blinks_unclassified_resets_in_cyan(void) {
    start_led_only(BSP_BOOT_OTHER, 0, 0, 255, 255, 3);
}

void test_usb_free_image_blinks_the_retained_marker_in_red_after_a_watchdog_reset(void) {
    start_led_only(BSP_BOOT_WATCHDOG, APPLICATION_DIAGNOSTICS_MARKER_COMMAND, 255, 0, 0, 4);
}

void test_watchdog_blink_count_is_clamped_to_a_readable_range(void) {
    start_led_only(BSP_BOOT_WATCHDOG, 0, 255, 0, 0, 1);
    start_led_only(BSP_BOOT_WATCHDOG, 20, 255, 0, 0, 8);
}

void test_poll_feeds_the_watchdog_and_marks_the_loop_before_any_deadline(void) {
    start_usb_at(0);

    poll_at(10);

    TEST_ASSERT_EQUAL_UINT32(1, mock_bsp_watchdog_feed_count());
    TEST_ASSERT_EQUAL_UINT32(APPLICATION_DIAGNOSTICS_MARKER_LOOP, mock_bsp_watchdog_marker());
}

void test_led_heartbeat_toggles_at_two_hertz(void) {
    start_usb_at(0);

    poll_at(249);
    bsp_led_off_Expect();
    poll_at(250);
    bsp_led_set_Expect(0, 0, 255, BRIGHTNESS);
    poll_at(500);
    bsp_led_off_Expect();
    poll_at(750);

    TEST_ASSERT_EQUAL_UINT32(4, mock_bsp_watchdog_feed_count());
}

void test_sample_shows_green_and_snapshots_health_while_traffic_advances(void) {
    start_usb_at(0);
    bsp_led_off_Expect();
    poll_at(250);

    mock_bsp_usb_set_health(health_of(true, false, 64, 5, 100));
    expect_sample(0, 255, 0);
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32(1, mock_bsp_watchdog_snapshot(0));
    TEST_ASSERT_EQUAL_UINT32(5, mock_bsp_watchdog_snapshot(1));
    TEST_ASSERT_EQUAL_UINT32(100u | (1u << 16), mock_bsp_watchdog_snapshot(2));
    TEST_ASSERT_TRUE(
        mock_bsp_watchdog_marker_was_written(APPLICATION_DIAGNOSTICS_MARKER_USB_SNAPSHOT));
    TEST_ASSERT_TRUE(
        mock_bsp_watchdog_marker_was_written(APPLICATION_DIAGNOSTICS_MARKER_FPGA_CHECK));
    TEST_ASSERT_EQUAL_UINT32(APPLICATION_DIAGNOSTICS_MARKER_LOOP, mock_bsp_watchdog_marker());
}

void test_heartbeat_stays_blue_while_the_host_has_not_asserted_dtr(void) {
    start_usb_at(0);
    bsp_led_off_Expect();
    poll_at(250);

    mock_bsp_usb_set_health(health_of(false, false, 64, 5, 100));
    expect_sample(0, 0, 255);
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32(100u, mock_bsp_watchdog_snapshot(2));
}

void test_heartbeat_turns_magenta_while_the_bus_is_suspended(void) {
    start_usb_at(0);
    bsp_led_off_Expect();
    poll_at(250);

    mock_bsp_usb_set_health(health_of(true, true, 64, 5, 100));
    expect_sample(255, 0, 255);
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32(100u | (1u << 16) | (1u << 17), mock_bsp_watchdog_snapshot(2));
}

void test_heartbeat_turns_magenta_when_the_frame_counter_freezes(void) {
    start_usb_at(0);
    bsp_led_off_Expect();
    poll_at(250);

    mock_bsp_usb_set_health(health_of(true, false, 64, 5, 100));
    expect_sample(0, 255, 0);
    poll_at(1000);

    bsp_led_off_Expect();
    poll_at(1250);

    /* transfers still complete, but the host has stopped sending start-of-frame */
    mock_bsp_usb_set_health(health_of(true, false, 64, 6, 100));
    expect_sample(255, 0, 255);
    poll_at(6000);
}

void test_heartbeat_turns_red_when_transfers_stop_completing(void) {
    start_usb_at(0);
    bsp_led_off_Expect();
    poll_at(250);

    mock_bsp_usb_set_health(health_of(true, false, 64, 5, 100));
    expect_sample(0, 255, 0);
    poll_at(1000);

    bsp_led_off_Expect();
    poll_at(1250);

    /* the bus is alive but no CDC transfer has completed for five seconds */
    mock_bsp_usb_set_health(health_of(true, false, 0, 5, 101));
    expect_sample(255, 0, 0);
    poll_at(6000);

    TEST_ASSERT_EQUAL_UINT32(101u | (1u << 16) | (1u << 18), mock_bsp_watchdog_snapshot(2));
}

void test_a_sample_without_a_heartbeat_toggle_still_refreshes_the_led(void) {
    start_usb_at(0);
    bsp_led_off_Expect();
    poll_at(999);

    /* 999 moved the heartbeat deadline to 1249, so only the sample is due here */
    mock_bsp_usb_set_health(health_of(true, false, 64, 5, 100));
    bsp_led_off_Expect();
    bsp_fpga_cdone_ExpectAndReturn(true);
    bsp_fpga_ping_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    bsp_led_get_ExpectAndReturn(led_state(0, 0, 255, false));
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32(1, mock_bsp_watchdog_snapshot(0));
}

void test_lost_configuration_reconfigures_and_flies_the_recovery_signature(void) {
    start_usb_at(0);
    bsp_led_off_Expect();
    poll_at(250);

    mock_bsp_usb_set_health(health_of(true, false, 64, 5, 100));
    bsp_led_set_Expect(0, 255, 0, BRIGHTNESS);
    bsp_fpga_cdone_ExpectAndReturn(false);
    bsp_fpga_reconfigure_ExpectAndReturn(true);
    bsp_led_set_Expect(255, 255, 255, BRIGHTNESS);
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32(100u | (1u << 16) | (1u << 19) | (1u << 26),
                             mock_bsp_watchdog_snapshot(2));

    /* the signature survives the following toggles, then healthy color resumes */
    bsp_led_off_Expect();
    poll_at(1250);
    bsp_led_set_Expect(255, 255, 255, BRIGHTNESS);
    poll_at(1500);
    bsp_led_off_Expect();
    poll_at(1750);
    bsp_led_set_Expect(255, 255, 255, BRIGHTNESS);
    bsp_fpga_cdone_ExpectAndReturn(true);
    bsp_fpga_ping_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    bsp_led_get_ExpectAndReturn(led_state(255, 255, 255, true));
    poll_at(2000);
    bsp_led_off_Expect();
    poll_at(2250);
    bsp_led_set_Expect(255, 255, 255, BRIGHTNESS);
    poll_at(2500);
    bsp_led_off_Expect();
    poll_at(2750);
    expect_sample(0, 255, 0);
    poll_at(3000);
}

void test_a_wrong_design_id_reconfigures_without_reading_the_led_back(void) {
    start_usb_at(0);
    bsp_led_off_Expect();
    poll_at(250);

    mock_bsp_usb_set_health(health_of(true, false, 64, 5, 100));
    bsp_led_set_Expect(0, 255, 0, BRIGHTNESS);
    bsp_fpga_cdone_ExpectAndReturn(true);
    bsp_fpga_ping_ExpectAndReturn(0x00);
    bsp_fpga_reconfigure_ExpectAndReturn(true);
    bsp_led_set_Expect(255, 255, 255, BRIGHTNESS);
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32((1u << 19) | (1u << 26),
                             mock_bsp_watchdog_snapshot(2) & ((0x7fu << 19) | (0x3fu << 26)));
}

/* A failed reconfiguration must leave the failure counted but fly no recovery
   signature, so the operator can tell "self-healed" from "still broken". */
static void run_readback_mismatch(bsp_led_state_t readback, const char *field_name) {
    mock_bsp_console_reset();
    mock_bsp_time_reset();
    mock_bsp_usb_reset();
    mock_bsp_watchdog_reset();

    start_usb_at(0);
    bsp_led_off_Expect();
    poll_at(250);

    mock_bsp_usb_set_health(health_of(true, false, 64, 5, 100));
    bsp_led_set_Expect(0, 255, 0, BRIGHTNESS);
    bsp_fpga_cdone_ExpectAndReturn(true);
    bsp_fpga_ping_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    bsp_led_get_ExpectAndReturn(readback);
    bsp_fpga_reconfigure_ExpectAndReturn(false);
    poll_at(1000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u << 19, mock_bsp_watchdog_snapshot(2) & (0x7fu << 19),
                                     field_name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, mock_bsp_watchdog_snapshot(2) & (0x3fu << 26), field_name);
}

void test_each_led_register_readback_mismatch_is_treated_as_an_fpga_fault(void) {
    const char *field_names[] = {"red", "green", "blue", "brightness", "enabled"};
    bsp_led_state_t mismatches[5];

    for (size_t index = 0; index < 5; ++index) {
        mismatches[index] = led_state(0, 255, 0, true);
    }
    mismatches[0].red = 1;
    mismatches[1].green = 254;
    mismatches[2].blue = 1;
    mismatches[3].brightness = BRIGHTNESS - 1;
    mismatches[4].enabled = false;

    for (size_t index = 0; index < 5; ++index) {
        run_readback_mismatch(mismatches[index], field_names[index]);
    }
}

void test_diag_report_lists_the_previous_boot_and_the_live_counters(void) {
    mock_bsp_watchdog_set_boot_reason(BSP_BOOT_BROWNOUT);
    mock_bsp_watchdog_set_retained(2, 7, 8, 9);
    start_usb_at(0);
    bsp_led_off_Expect();
    poll_at(250);
    mock_bsp_usb_set_health(health_of(true, false, 32, 5, 100));
    expect_sample(0, 255, 0);
    poll_at(1000);
    mock_bsp_console_reset();

    application_diagnostics_print_report();

    TEST_ASSERT_EQUAL_STRING(
        "diag: boot=brownout marker=2 loop=7 usb=8 health=00000009\n"
        "diag: uptime=1s connected=1 suspended=0 write=32 activity=5 sof=100 "
        "fpga_fail=0 fpga_reconfig=0\n",
        mock_bsp_console_output());
}
