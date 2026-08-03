#include "unity.h"

#include <stdint.h>
#include <string.h>

#include "application.h"
#include "application_console.h"
#include "application_diagnostics.h"
#include "mock_bsp_console.h"
#include "mock_bsp_time.h"
#include "mock_bsp_usb.h"
#include "mock_bsp_watchdog.h"
#include "mock_auto_bsp_button.h"
#include "mock_auto_bsp_fpga.h"
#include "mock_auto_bsp_led.h"
#include "mock_auto_bsp_memory.h"

void setUp(void) {
    mock_bsp_console_reset();
    mock_bsp_time_reset();
    mock_bsp_usb_reset();
    mock_bsp_watchdog_reset();
}

void tearDown(void) {
}

static void start_at(uint32_t now_ms) {
    mock_bsp_time_set_ms(now_ms);
    application_console_start();
}

static void poll_at(uint32_t now_ms) {
    mock_bsp_time_set_ms(now_ms);
    application_console_poll();
}

static void poll_text_at(const char *text, uint32_t now_ms) {
    mock_bsp_console_queue_text(text);
    for (size_t index = 0; index < strlen(text); ++index) {
        poll_at(now_ms);
    }
}

static void expect_ready_status(uint8_t count) {
    bsp_button_state_t button = {.level = 0x03, .count = count};
    BSP_FpgaIsReady_ExpectAndReturn(true);
    BSP_ButtonGetState_ExpectAndReturn(button);
    BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_FpgaReadStatus_ExpectAndReturn(0x01);
    BSP_FpgaStatusPin_ExpectAndReturn(true);
}

void test_console_starts_with_a_prompt_and_reports_boot_status_each_second(void) {
    start_at(500);
    TEST_ASSERT_EQUAL_STRING("forgix> ", mock_bsp_console_output());

    poll_at(1499);
    expect_ready_status(7);
    poll_at(1500);
    expect_ready_status(8);
    poll_at(2500);

    TEST_ASSERT_EQUAL_STRING(
        "forgix> \r\nid=B5 status=01 button=03 count=7 fpga_status=1\n"
        "forgix> \r\nid=B5 status=01 button=03 count=8 fpga_status=1\nforgix> ",
        mock_bsp_console_output());
}

void test_received_character_wins_over_a_due_status_and_protects_partial_input(void) {
    start_at(0);
    mock_bsp_console_reset();

    mock_bsp_console_queue_character('h');
    poll_at(APPLICATION_BOOT_STATUS_PERIOD_MS);
    poll_at(APPLICATION_IDLE_TIMEOUT_MS * 2u);

    TEST_ASSERT_EQUAL_STRING("h", mock_bsp_console_output());
}

void test_console_echoes_a_command_and_coalesces_crlf(void) {
    start_at(0);
    mock_bsp_console_reset();

    poll_text_at("help\r\n", 100);

    TEST_ASSERT_EQUAL_STRING(
        "help\r\n"
        "hello | color <r> <g> <b> [brightness] | off | status | diag | reset | "
        "echo <on|off> | watch <seconds|off> | quiet | interactive | help\n"
        "forgix> ",
        mock_bsp_console_output());
}

void test_console_accepts_lf_and_empty_lines(void) {
    start_at(0);
    mock_bsp_console_reset();

    poll_text_at("\n", 100);

    TEST_ASSERT_EQUAL_STRING("\r\nforgix> ", mock_bsp_console_output());
}

void test_backspace_delete_and_ctrl_u_edit_the_local_line(void) {
    start_at(0);
    mock_bsp_console_reset();

    mock_bsp_console_queue_character('a');
    mock_bsp_console_queue_character('\b');
    mock_bsp_console_queue_character('\b');
    mock_bsp_console_queue_character('b');
    mock_bsp_console_queue_character(127);
    mock_bsp_console_queue_text("cd");
    mock_bsp_console_queue_character(21);
    for (int index = 0; index < 8; ++index) {
        poll_at(100);
    }

    TEST_ASSERT_EQUAL_STRING("a\b \b\ab\b \bcd\b \b\b \b",
                             mock_bsp_console_output());
}

void test_ctrl_c_cancels_input_and_ctrl_l_redraws_it(void) {
    start_at(0);
    mock_bsp_console_reset();

    poll_text_at("xy", 100);
    mock_bsp_console_queue_character(12);
    poll_at(100);
    mock_bsp_console_queue_character(3);
    poll_at(100);

    TEST_ASSERT_EQUAL_STRING("xy\r\nforgix> xy^C\r\nforgix> ",
                             mock_bsp_console_output());
}

void test_nonprinting_input_is_ignored_and_overflow_rings_the_bell(void) {
    start_at(0);
    mock_bsp_console_reset();

    mock_bsp_console_queue_character(1);
    poll_at(100);
    for (int index = 0; index < 128; ++index) {
        mock_bsp_console_queue_character('x');
        poll_at(100);
    }

    TEST_ASSERT_EQUAL_UINT32(128, strlen(mock_bsp_console_output()));
    TEST_ASSERT_EQUAL_CHAR('\a', mock_bsp_console_output()[127]);
}

void test_completed_command_resumes_periodic_status_after_the_idle_timeout(void) {
    start_at(0);
    mock_bsp_console_reset();
    poll_text_at("help\r", 100);
    mock_bsp_console_reset();

    poll_at(10099);
    expect_ready_status(9);
    poll_at(10100);
    expect_ready_status(10);
    poll_at(20100);

    TEST_ASSERT_EQUAL_STRING(
        "\r\nid=B5 status=01 button=03 count=9 fpga_status=1\n"
        "forgix> \r\nid=B5 status=01 button=03 count=10 fpga_status=1\nforgix> ",
        mock_bsp_console_output());
}

void test_watch_uses_the_requested_period_and_stops_before_echoing_a_key(void) {
    start_at(0);
    mock_bsp_console_reset();
    poll_text_at("watch 2\r", 100);
    mock_bsp_console_reset();

    poll_at(2099);
    expect_ready_status(11);
    poll_at(2100);
    mock_bsp_console_queue_character('h');
    poll_at(4100);
    poll_at(20000);

    TEST_ASSERT_EQUAL_STRING(
        "\r\nid=B5 status=01 button=03 count=11 fpga_status=1\nforgix> h",
        mock_bsp_console_output());
}

void test_watch_off_suppresses_idle_status_until_interactive_mode_is_restored(void) {
    start_at(0);
    mock_bsp_console_reset();
    poll_text_at("watch off\r", 100);
    mock_bsp_console_reset();

    poll_at(50000);
    TEST_ASSERT_EQUAL_STRING("", mock_bsp_console_output());

    poll_text_at("interactive\r", 50100);
    mock_bsp_console_reset();
    expect_ready_status(12);
    poll_at(60100);
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "count=12"));
}

void test_quiet_mode_keeps_machine_commands_free_of_echo_prompts_and_telemetry(void) {
    start_at(0);
    mock_bsp_console_reset();
    poll_text_at("quiet\r", 100);

    TEST_ASSERT_EQUAL_STRING("quiet\r\nok\n", mock_bsp_console_output());
    mock_bsp_console_reset();
    poll_at(50000);
    mock_bsp_console_queue_character('x');
    mock_bsp_console_queue_character('\b');
    poll_at(50001);
    poll_at(50002);
    poll_text_at("help\r", 50100);

    TEST_ASSERT_EQUAL_STRING(
        "hello | color <r> <g> <b> [brightness] | off | status | diag | reset | "
        "echo <on|off> | watch <seconds|off> | quiet | interactive | help\n",
        mock_bsp_console_output());
}

void test_interactive_mode_restores_echo_prompt_and_idle_reporting(void) {
    start_at(0);
    poll_text_at("quiet\r", 100);
    mock_bsp_console_reset();

    poll_text_at("interactive\r", 200);
    mock_bsp_console_queue_character('x');
    poll_at(201);

    TEST_ASSERT_EQUAL_STRING("ok\nforgix> x", mock_bsp_console_output());
}

void test_echo_can_be_disabled_and_reenabled_without_changing_command_responses(void) {
    start_at(0);
    mock_bsp_console_reset();
    poll_text_at("echo off\r", 100);
    mock_bsp_console_reset();

    mock_bsp_console_queue_character('x');
    mock_bsp_console_queue_character('\b');
    poll_at(150);
    poll_at(151);
    poll_text_at("help\r", 200);
    TEST_ASSERT_EQUAL_STRING(
        "hello | color <r> <g> <b> [brightness] | off | status | diag | reset | "
        "echo <on|off> | watch <seconds|off> | quiet | interactive | help\nforgix> ",
        mock_bsp_console_output());

    mock_bsp_console_reset();
    poll_text_at("echo on\r", 300);
    mock_bsp_console_queue_character('x');
    poll_at(301);
    TEST_ASSERT_EQUAL_STRING("ok\nforgix> x", mock_bsp_console_output());
}

void test_unsolicited_status_is_withheld_until_the_host_asserts_dtr(void) {
    mock_bsp_usb_set_connected(false);
    start_at(500);
    mock_bsp_console_reset();

    poll_at(1500);
    poll_at(2500);
    TEST_ASSERT_EQUAL_STRING("", mock_bsp_console_output());

    mock_bsp_usb_set_connected(true);
    expect_ready_status(13);
    poll_at(3500);
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "count=13"));
}

void test_console_marks_the_read_write_and_command_paths_for_the_watchdog(void) {
    start_at(0);
    TEST_ASSERT_EQUAL_UINT32(APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE,
                             mock_bsp_watchdog_marker());

    poll_at(10);
    TEST_ASSERT_TRUE(
        mock_bsp_watchdog_marker_was_written(APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_READ));

    poll_text_at("help\r", 100);
    TEST_ASSERT_TRUE(mock_bsp_watchdog_marker_was_written(APPLICATION_DIAGNOSTICS_MARKER_COMMAND));
}

void test_ctrl_c_and_ctrl_l_remain_silent_in_quiet_mode(void) {
    start_at(0);
    poll_text_at("quiet\r", 100);
    mock_bsp_console_reset();

    mock_bsp_console_queue_character(12);
    mock_bsp_console_queue_character(3);
    poll_at(200);
    poll_at(200);

    TEST_ASSERT_EQUAL_STRING("", mock_bsp_console_output());
}
