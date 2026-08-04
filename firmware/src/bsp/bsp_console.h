#ifndef FORGIX_BSP_CONSOLE_H
#define FORGIX_BSP_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_types.h"




/***************************************************************************************
**
** Compiler Define Directives
**
***************************************************************************************/


/* Returned by BSP_ConsoleGetCharTimeoutUs in place of a received byte when the
   read window elapses with nothing on the wire. Negative so it can never be
   confused with a real byte value, which is always in 0..255. */
#define BSP_CONSOLE_TIMEOUT ( ( int16_t ) - 1 )




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


void BSP_ConsoleInit( void );

int16_t BSP_ConsoleGetCharTimeoutUs( const uint32_t timeoutUs );

int16_t BSP_ConsolePutChar( const uint8_t character );

int32_t BSP_ConsolePrintf( const char *const ptr_format, ... );

int32_t BSP_ConsolePuts( const char *const ptr_text );

#ifdef __cplusplus
}
#endif

#endif
