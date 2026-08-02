#include "mock_bsp_console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char output[2048];
static int input[512];
static size_t input_count;
static size_t input_position;

static void append(const char *text) {
    size_t used = strlen(output);
    size_t remaining = sizeof output - used;
    if (remaining > 1) {
        snprintf(output + used, remaining, "%s", text);
    }
}

void mock_bsp_console_reset(void) {
    output[0] = 0;
    input_count = 0;
    input_position = 0;
}

void mock_bsp_console_queue_character(int character) {
    if (input_count < sizeof input / sizeof input[0]) {
        input[input_count++] = character;
    }
}

void mock_bsp_console_queue_text(const char *text) {
    while (*text) {
        mock_bsp_console_queue_character((unsigned char)*text++);
    }
}

const char *mock_bsp_console_output(void) {
    return output;
}

void bsp_console_init(void) {
}

int bsp_console_getchar_timeout_us(uint32_t timeout_us) {
    (void)timeout_us;
    if (input_position < input_count) {
        return input[input_position++];
    }
    return BSP_CONSOLE_TIMEOUT;
}

int bsp_console_putchar(int character) {
    char text[2] = {(char)character, 0};
    append(text);
    return character;
}

int bsp_console_printf(const char *format, ...) {
    char formatted[512];
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(formatted, sizeof formatted, format, arguments);
    va_end(arguments);
    append(formatted);
    return result;
}

int bsp_console_puts(const char *text) {
    append(text);
    append("\n");
    return 0;
}
