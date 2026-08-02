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

static void process(const char *command) {
    char mutable_command[128];
    snprintf(mutable_command, sizeof mutable_command, "%s", command);
    application_process_command(mutable_command);
}

static bsp_memory_report_t memory_report(void);

/* The reporting layer names the bus clock each sweep entry was taken at, so the
   divisor table has to be available too. */
static const uint8_t probe_divisors[4] = {4u, 6u, 8u, 16u};

static void expect_memory_report(void) {
    bsp_memory_check_ExpectAndReturn(memory_report());
    bsp_memory_probe_clkdivs_ExpectAndReturn(probe_divisors);
}

static bsp_memory_report_t memory_report(void) {
    bsp_memory_report_t report = {
        .flash_bytes = 2u * 1024u * 1024u,
        .flash_ok = true,
        .psram_bytes = 8u * 1024u * 1024u,
        .psram_ok = true,
    };
    return report;
}

static bsp_led_state_t expected_hello_led(void) {
    bsp_led_state_t led = {
        .red = 0,
        .green = 255,
        .blue = 255,
        .brightness = 64,
        .enabled = true,
    };
    return led;
}

static void expect_hello_readback(uint8_t design_id, bsp_led_state_t led) {
    bsp_fpga_is_ready_ExpectAndReturn(true);
    bsp_led_set_Expect(0, 255, 255, 64);
    bsp_fpga_ping_ExpectAndReturn(design_id);
    bsp_led_get_ExpectAndReturn(led);
}

void test_application_init_reports_ready_hardware_and_help(void) {
    bsp_init_result_t result = {
        .configured = true,
        .design_id = BSP_FPGA_DESIGN_ID,
        .ready = true,
        .cdone = true,
        .status_pin = true,
    };

    expect_memory_report();
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

    expect_memory_report();
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

void test_color_rejects_trailing_characters_without_writing_hardware(void) {
    bsp_fpga_is_ready_ExpectAndReturn(true);

    process("color 1 2 3x");

    TEST_ASSERT_EQUAL_STRING("error: values must be 0..255\n", mock_bsp_console_output());
}

void test_color_rejects_the_wrong_argument_count(void) {
    bsp_fpga_is_ready_ExpectAndReturn(true);

    process("color 1 2");

    TEST_ASSERT_EQUAL_STRING("error: invalid command (try help)\n", mock_bsp_console_output());
}

void test_hello_programs_and_verifies_the_expected_led_state(void) {
    expect_hello_readback(BSP_FPGA_DESIGN_ID, expected_hello_led());

    process("hello");

    TEST_ASSERT_EQUAL_STRING("Hello from RP2354 -> FPGA B5\n", mock_bsp_console_output());
}

void test_hello_reports_failed_readback(void) {
    bsp_led_state_t led = {0};
    expect_hello_readback(0, led);

    process("hello");

    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "hello readback failed"));
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "id=00"));
}

void test_hello_reports_each_led_readback_mismatch(void) {
    const char *field_names[] = {"red", "green", "blue", "brightness", "enabled"};
    bsp_led_state_t mismatches[5];

    for (size_t index = 0; index < 5; ++index) {
        mismatches[index] = expected_hello_led();
    }
    mismatches[0].red = 1;
    mismatches[1].green = 254;
    mismatches[2].blue = 254;
    mismatches[3].brightness = 63;
    mismatches[4].enabled = false;

    for (size_t index = 0; index < 5; ++index) {
        expect_hello_readback(BSP_FPGA_DESIGN_ID, mismatches[index]);
        process("hello");

        TEST_ASSERT_NOT_NULL_MESSAGE(
            strstr(mock_bsp_console_output(), "hello readback failed"), field_names[index]);
        mock_bsp_console_reset();
    }
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

void test_console_control_commands_remain_available_without_fpga_access(void) {
    const char *commands[] = {
        "echo on",
        "echo off",
        "watch 1",
        "watch 3600",
        "watch off",
        "quiet",
        "interactive",
    };

    for (size_t index = 0; index < sizeof commands / sizeof commands[0]; ++index) {
        process(commands[index]);
        TEST_ASSERT_EQUAL_STRING("ok\n", mock_bsp_console_output());
        mock_bsp_console_reset();
    }
}

void test_console_control_commands_report_invalid_usage(void) {
    const char *commands[] = {
        "echo",
        "echo maybe",
        "echo on extra",
        "watch",
        "watch nope",
        "watch 1x",
        "watch 0",
        "watch 3601",
        "watch off extra",
        "quiet extra",
        "interactive extra",
    };

    for (size_t index = 0; index < sizeof commands / sizeof commands[0]; ++index) {
        process(commands[index]);
        TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "error:"));
        mock_bsp_console_reset();
    }
}

void test_status_reports_unavailable_hardware_without_accessing_registers(void) {
    bsp_fpga_is_ready_ExpectAndReturn(false);

    process("status");

    TEST_ASSERT_EQUAL_STRING("status unavailable: FPGA is not configured and responding\n",
                             mock_bsp_console_output());
}

void test_diag_reports_the_last_reset_and_stays_available_without_fpga_access(void) {
    mock_bsp_watchdog_set_boot_reason(BSP_BOOT_WATCHDOG);
    mock_bsp_watchdog_set_retained(APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE, 0, 0, 0);
    expect_memory_report();

    process("diag");

    /* both QSPI memories, so a shared-bus fault is visible without a reboot */
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "flash=2048KiB ok=1"));
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "psram=8192KiB ok=1"));
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "diag: boot="));
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "uptime="));
    TEST_ASSERT_NOT_NULL(strstr(mock_bsp_console_output(), "fpga_reconfig="));
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

void test_known_commands_with_extra_arguments_are_rejected(void) {
    const char *commands[] = {
        "help extra",
        "hello extra",
        "off extra",
        "status extra",
        "diag extra",
        "reset extra",
    };

    for (size_t index = 0; index < sizeof commands / sizeof commands[0]; ++index) {
        bsp_fpga_is_ready_ExpectAndReturn(true);
        process(commands[index]);

        TEST_ASSERT_EQUAL_STRING("error: invalid command (try help)\n",
                                 mock_bsp_console_output());
        mock_bsp_console_reset();
    }
}

void test_command_tokenization_is_safely_limited_to_the_argument_capacity(void) {
    bsp_fpga_is_ready_ExpectAndReturn(true);

    process("unknown 1 2 3 4 5 6");

    TEST_ASSERT_EQUAL_STRING("error: invalid command (try help)\n", mock_bsp_console_output());
}
