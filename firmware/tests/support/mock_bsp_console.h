#ifndef FORGIX_MOCK_BSP_CONSOLE_H
#define FORGIX_MOCK_BSP_CONSOLE_H

#include "bsp_console.h"
#include "bsp_types.h"

void MOCK_BSP_ConsoleReset( void );
void MOCK_BSP_ConsoleQueueCharacter( const uint8_t character );
void MOCK_BSP_ConsoleQueueText( const char *text );
const char *MOCK_BSP_ConsoleOutput( void );

#endif
