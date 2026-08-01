#include "fake_bsp_console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char output[2048];

static void append(const char *text) {
    size_t used = strlen(output);
    size_t remaining = sizeof output - used;
    if (remaining > 1) {
        snprintf(output + used, remaining, "%s", text);
    }
}

void fake_bsp_console_reset(void) {
    output[0] = 0;
}

const char *fake_bsp_console_output(void) {
    return output;
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
    append(formatted);
    return result;
}

int bsp_console_puts(const char *text) {
    append(text);
    append("\n");
    return 0;
}
