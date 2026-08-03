#ifndef FORGIX_BSP_CONSOLE_H
#define FORGIX_BSP_CONSOLE_H

#include <stdint.h>

enum { BSP_CONSOLE_TIMEOUT = -1 };

void bsp_console_init(void);
int bsp_console_getchar_timeout_us(const uint32_t timeout_us);
int bsp_console_putchar(const int character);
int bsp_console_printf(const char *const format, ...);
int bsp_console_puts(const char *const text);

#endif
