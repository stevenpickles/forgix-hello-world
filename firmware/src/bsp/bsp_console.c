#include "bsp_console.h"

#include <stdarg.h>
#include <stdio.h>

#include "pico/stdlib.h"

void bsp_console_init(void) {
    stdio_init_all();
}

int bsp_console_getchar_timeout_us(uint32_t timeout_us) {
    int character = getchar_timeout_us(timeout_us);
    return character == PICO_ERROR_TIMEOUT ? BSP_CONSOLE_TIMEOUT : character;
}

int bsp_console_putchar(int character) {
    return putchar(character);
}

int bsp_console_printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vprintf(format, arguments);
    va_end(arguments);
    return result;
}

int bsp_console_puts(const char *text) {
    return puts(text);
}
