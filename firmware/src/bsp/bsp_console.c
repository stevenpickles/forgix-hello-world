#include "bsp_console.h"

#include <stdarg.h>
#include <stdio.h>

#include "pico/stdlib.h"

void BSP_ConsoleInit(void)
{
    stdio_init_all();
}

int BSP_ConsoleGetCharTimeoutUs(const uint32_t timeout_us)
{
    const int character = getchar_timeout_us(timeout_us);
    if (character == PICO_ERROR_TIMEOUT)
    {
        return BSP_CONSOLE_TIMEOUT;
    }
    return character;
}

int BSP_ConsolePutChar(const int character)
{
    return putchar(character);
}

int BSP_ConsolePrintf(const char *const format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int result = vprintf(format, arguments);
    va_end(arguments);
    return result;
}

int BSP_ConsolePuts(const char *const text)
{
    return puts(text);
}
