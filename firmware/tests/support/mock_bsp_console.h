#ifndef FORGIX_MOCK_BSP_CONSOLE_H
#define FORGIX_MOCK_BSP_CONSOLE_H

#include "bsp_console.h"

void mock_bsp_console_reset(void);
void mock_bsp_console_queue_character(int character);
void mock_bsp_console_queue_text(const char *text);
const char *mock_bsp_console_output(void);

#endif
