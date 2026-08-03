#include "mock_bsp_console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char output[2048];
static int input[512];
static size_t input_count;
static size_t input_position;

static void append(const char *const text)
{
    size_t used = strlen(output);
    size_t remaining = sizeof output - used;
    if (remaining > 1)
    {
        snprintf(output + used, remaining, "%s", text);
    }
}

void MOCK_BSP_ConsoleReset(void)
{
    output[0] = 0;
    input_count = 0;
    input_position = 0;
}

void MOCK_BSP_ConsoleQueueCharacter(const int character)
{
    if (input_count < sizeof input / sizeof input[0])
    {
        input[input_count++] = character;
    }
}

void MOCK_BSP_ConsoleQueueText(const char *text)
{
    while (*text)
    {
        MOCK_BSP_ConsoleQueueCharacter((unsigned char)*text++);
    }
}

const char *MOCK_BSP_ConsoleOutput(void)
{
    return output;
}

void BSP_ConsoleInit(void)
{
}

int BSP_ConsoleGetCharTimeoutUs(const uint32_t timeout_us)
{
    (void)timeout_us;
    if (input_position < input_count)
    {
        return input[input_position++];
    }
    return BSP_CONSOLE_TIMEOUT;
}

int BSP_ConsolePutChar(const int character)
{
    char text[2] = {(char)character, 0};
    append(text);
    return character;
}

int BSP_ConsolePrintf(const char *const format, ...)
{
    char formatted[512];
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(formatted, sizeof formatted, format, arguments);
    va_end(arguments);
    append(formatted);
    return result;
}

int BSP_ConsolePuts(const char *const text)
{
    append(text);
    append("\n");
    return 0;
}
