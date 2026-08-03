#ifndef FORGIX_BSP_CONSOLE_H
#define FORGIX_BSP_CONSOLE_H

#include "bsp_types.h"

enum
{
    BSP_CONSOLE_TIMEOUT = -1
};

void BSP_ConsoleInit(void);
int BSP_ConsoleGetCharTimeoutUs(const uint32_t timeout_us);
int BSP_ConsolePutChar(const int character);
int BSP_ConsolePrintf(const char *const format, ...);
int BSP_ConsolePuts(const char *const text);

#endif
