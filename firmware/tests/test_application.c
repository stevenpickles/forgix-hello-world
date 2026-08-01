#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "application.h"
#include "bsp.h"

static char console_output[2048];
static bool fpga_ready;
static uint8_t design_id;
static uint8_t fpga_status;
static bool fpga_status_pin_value;
static bsp_led_state_t led_state;
static bsp_button_state_t button_state;
static unsigned led_set_count;
static unsigned led_off_count;
static unsigned fpga_reset_count;
static int failures;

static void append_output(const char *text) {
    size_t used = strlen(console_output);
    size_t remaining = sizeof console_output - used;
    if (remaining > 1) {
        snprintf(console_output + used, remaining, "%s", text);
    }
}

static void reset_mocks(void) {
    memset(console_output, 0, sizeof console_output);
    fpga_ready = true;
    design_id = BSP_FPGA_DESIGN_ID;
    fpga_status = 0x01;
    fpga_status_pin_value = true;
    led_state = (bsp_led_state_t){0};
    button_state = (bsp_button_state_t){.level = 0x03, .count = 7};
    led_set_count = 0;
    led_off_count = 0;
    fpga_reset_count = 0;
}

static void check(bool condition, const char *description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}

static void check_output_contains(const char *expected) {
    char description[256];
    snprintf(description, sizeof description, "output contains '%s'", expected);
    check(strstr(console_output, expected) != NULL, description);
}

static void process(const char *command) {
    char mutable_command[128];
    snprintf(mutable_command, sizeof mutable_command, "%s", command);
    application_process_command(mutable_command);
}

void bsp_console_init(void) {
}

int bsp_console_getchar_timeout_us(uint32_t timeout_us) {
    (void)timeout_us;
    return BSP_CONSOLE_TIMEOUT;
}

int bsp_console_printf(const char *format, ...) {
    char formatted[512];
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(formatted, sizeof formatted, format, arguments);
    va_end(arguments);
    append_output(formatted);
    return result;
}

int bsp_console_puts(const char *text) {
    append_output(text);
    append_output("\n");
    return 0;
}

bool bsp_fpga_is_ready(void) {
    return fpga_ready;
}

uint8_t bsp_fpga_ping(void) {
    return design_id;
}

uint8_t bsp_fpga_read_status(void) {
    return fpga_status;
}

bool bsp_fpga_status_pin(void) {
    return fpga_status_pin_value;
}

void bsp_fpga_reset(void) {
    ++fpga_reset_count;
}

uint8_t bsp_fpga_read_register(uint8_t address) {
    (void)address;
    return 0;
}

void bsp_fpga_write_register(uint8_t address, uint8_t value) {
    (void)address;
    (void)value;
}

void bsp_led_set(uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness) {
    led_state = (bsp_led_state_t){
        .red = red,
        .green = green,
        .blue = blue,
        .brightness = brightness,
        .enabled = true,
    };
    ++led_set_count;
}

void bsp_led_off(void) {
    led_state.enabled = false;
    ++led_off_count;
}

bsp_led_state_t bsp_led_get(void) {
    return led_state;
}

bsp_button_state_t bsp_button_get_state(void) {
    return button_state;
}

static void test_ready_startup(void) {
    reset_mocks();
    bsp_init_result_t result = {
        .configured = true,
        .design_id = BSP_FPGA_DESIGN_ID,
        .ready = true,
        .cdone = true,
        .status_pin = true,
    };

    application_init(&result);

    check_output_contains("configuration=ok");
    check_output_contains("runtime=ready");
    check_output_contains("hello | color");
}

static void test_unavailable_hardware_keeps_help_available(void) {
    reset_mocks();
    fpga_ready = false;

    process("help");
    check_output_contains("hello | color");

    memset(console_output, 0, sizeof console_output);
    process("hello");
    check_output_contains("FPGA is not configured and responding");
    check(led_set_count == 0, "unavailable FPGA blocks LED writes");
}

static void test_color_commands(void) {
    reset_mocks();

    process("color 1 2 3");
    check(led_set_count == 1, "color command writes the LED once");
    check(led_state.red == 1 && led_state.green == 2 && led_state.blue == 3,
          "color command forwards RGB values");
    check(led_state.brightness == 255, "color command defaults brightness to 255");
    check_output_contains("ok");

    reset_mocks();
    process("color 4 5 6 7");
    check(led_state.brightness == 7, "color command forwards explicit brightness");

    reset_mocks();
    process("color 1 2 256");
    check(led_set_count == 0, "invalid color does not partially update hardware");
    check_output_contains("values must be 0..255");
}

static void test_runtime_commands(void) {
    reset_mocks();

    process("hello");
    check(led_set_count == 1, "hello programs the LED");
    check(led_state.red == 0 && led_state.green == 255 && led_state.blue == 255 &&
              led_state.brightness == 64 && led_state.enabled,
          "hello programs the expected cyan state");
    check_output_contains("Hello from RP2354 -> FPGA B5");

    reset_mocks();
    process("off");
    check(led_off_count == 1, "off disables the LED");

    reset_mocks();
    process("status");
    check_output_contains("id=B5 status=01 button=03 count=7 fpga_status=1");

    reset_mocks();
    process("reset");
    check(fpga_reset_count == 1, "reset reaches the FPGA BSP");

    reset_mocks();
    process("unknown");
    check_output_contains("invalid command");
}

int main(void) {
    test_ready_startup();
    test_unavailable_hardware_keeps_help_available();
    test_color_commands();
    test_runtime_commands();

    if (failures) {
        fprintf(stderr, "%d application test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("Application tests passed");
    return EXIT_SUCCESS;
}
