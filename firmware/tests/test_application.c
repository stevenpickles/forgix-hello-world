#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "application.h"
#include "mock_bsp_console.h"
#include "mock_auto_bsp_button.h"
#include "mock_auto_bsp_fpga.h"
#include "mock_auto_bsp_led.h"

void setUp(void) {
    mock_bsp_console_reset();
}

void tearDown(void) {
}

static void process(const char *command) {
    char mutable_command[128];
    snprintf(mutable_command, sizeof mutable_command, "%s", command);
    application_process_command(mutable_command);
}

void test_application_init_reports_ready_hardware_and_help(void) {
    bsp_init_result_t result = {
        .configured = true,
        .design_id = BSP_FPGA_DESIGN_ID,
        .ready = true,
        .cdone = true,
        .status_pin = true,
    };

    application_init(&result);

    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "configuration=ok"));
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "runtime=ready"));
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "hello | color"));
}

void test_application_init_preserves_diagnostics_when_hardware_is_unavailable(void) {
    bsp_init_result_t result = {
        .configured = false,
        .design_id = 0,
        .ready = false,
        .cdone = false,
        .status_pin = false,
    };

    application_init(&result);

    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "configuration=failed"));
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "runtime=unavailable"));
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "runtime commands are disabled"));
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "hello | color"));
}

void test_empty_command_has_no_effect(void) {
    process(" \t");
    TEST_ASSERT_EQUAL_STRING("", mock_bsp_console_output());
}

void test_help_remains_available_without_fpga_access(void) {
    process("help");
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "hello | color"));
}

void test_hardware_command_is_rejected_when_fpga_is_unavailable(void) {
    bsp_fpga_is_ready_ExpectAndReturn(false);

    process("hello");

    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "FPGA is not configured and responding"));
}

void test_color_uses_default_brightness(void) {
    bsp_fpga_is_ready_ExpectAndReturn(true);
    bsp_led_set_Expect(1, 2, 3, 255);

    process("color 1 2 3");

    TEST_ASSERT_EQUAL_STRING("ok\n", mock_bsp_console_output());
}

void test_color_forwards_explicit_brightness(void) {
    bsp_fpga_is_ready_ExpectAndReturn(true);
    bsp_led_set_Expect(4, 5, 6, 7);

    process("color 4 5 6 7");

    TEST_ASSERT_EQUAL_STRING("ok\n", mock_bsp_console_output());
}

void test_color_rejects_values_above_a_byte_without_writing_hardware(void) {
    bsp_fpga_is_ready_ExpectAndReturn(true);

    process("color 1 2 256");

    TEST_ASSERT_EQUAL_STRING("error: values must be 0..255\n", mock_bsp_console_output());
}

void test_color_rejects_negative_values_without_writing_hardware(void) {
    bsp_fpga_is_ready_ExpectAndReturn(true);

    process("color -1 2 3");

    TEST_ASSERT_EQUAL_STRING("error: values must be 0..255\n", mock_bsp_console_output());
}

void test_color_rejects_non_numeric_values_without_writing_hardware(void) {
    bsp_fpga_is_ready_ExpectAndReturn(true);

    process("color red 2 3");

    TEST_ASSERT_EQUAL_STRING("error: values must be 0..255\n", mock_bsp_console_output());
}

void test_color_rejects_the_wrong_argument_count(void) {
    bsp_fpga_is_ready_ExpectAndReturn(true);

    process("color 1 2");

    TEST_ASSERT_EQUAL_STRING("error: invalid command (try help)\n", mock_bsp_console_output());
}

void test_hello_programs_and_verifies_the_expected_led_state(void) {
    bsp_led_state_t led = {
        .red = 0,
        .green = 255,
        .blue = 255,
        .brightness = 64,
        .enabled = true,
    };
    bsp_fpga_is_ready_ExpectAndReturn(true);
    bsp_led_set_Expect(0, 255, 255, 64);
    bsp_fpga_ping_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    bsp_led_get_ExpectAndReturn(led);

    process("hello");

    TEST_ASSERT_EQUAL_STRING("Hello from RP2354 -> FPGA B5\n", mock_bsp_console_output());
}

void test_hello_reports_failed_readback(void) {
    bsp_led_state_t led = {0};
    bsp_fpga_is_ready_ExpectAndReturn(true);
    bsp_led_set_Expect(0, 255, 255, 64);
    bsp_fpga_ping_ExpectAndReturn(0);
    bsp_led_get_ExpectAndReturn(led);

    process("hello");

    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "hello readback failed"));
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "id=00"));
}

void test_off_disables_the_led(void) {
    bsp_fpga_is_ready_ExpectAndReturn(true);
    bsp_led_off_Expect();

    process("off");

    TEST_ASSERT_EQUAL_STRING("ok\n", mock_bsp_console_output());
}

void test_status_reports_fpga_and_button_state(void) {
    bsp_button_state_t button = {.level = 0x03, .count = 7};
    bsp_fpga_is_ready_ExpectAndReturn(true);
    bsp_button_get_state_ExpectAndReturn(button);
    bsp_fpga_ping_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    bsp_fpga_read_status_ExpectAndReturn(0x01);
    bsp_fpga_status_pin_ExpectAndReturn(true);

    process("status");

    TEST_ASSERT_EQUAL_STRING("id=B5 status=01 button=03 count=7 fpga_status=1\n",
                             mock_bsp_console_output());
}

void test_reset_reaches_the_fpga(void) {
    bsp_fpga_is_ready_ExpectAndReturn(true);
    bsp_fpga_reset_Expect();

    process("reset");

    TEST_ASSERT_EQUAL_STRING("ok\n", mock_bsp_console_output());
}

void test_unknown_command_is_rejected(void) {
    bsp_fpga_is_ready_ExpectAndReturn(true);

    process("unknown");

    TEST_ASSERT_EQUAL_STRING("error: invalid command (try help)\n", mock_bsp_console_output());
}
