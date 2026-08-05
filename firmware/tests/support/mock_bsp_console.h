#ifndef FORGIX_MOCK_BSP_CONSOLE_H
#define FORGIX_MOCK_BSP_CONSOLE_H


#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_console.h"
#include "bsp_types.h"




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


void MOCK_BSP_ConsoleReset( void );

void MOCK_BSP_ConsoleQueueCharacter( const uint8_t character );

void MOCK_BSP_ConsoleQueueResult( const int16_t result );

void MOCK_BSP_ConsoleQueueText( const char *ptr_text );

const char *MOCK_BSP_ConsoleOutput( void );




#ifdef __cplusplus
}
#endif


#endif
