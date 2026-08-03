#ifndef FORGIX_MOCK_BSP_CONSOLE_H
#define FORGIX_MOCK_BSP_CONSOLE_H

#include "bsp_console.h"

void MOCK_BSP_ConsoleReset(void);
void MOCK_BSP_ConsoleQueueCharacter(const int character);
void MOCK_BSP_ConsoleQueueText(const char *text);
const char *MOCK_BSP_ConsoleOutput(void);

#endif
