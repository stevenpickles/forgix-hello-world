#ifndef FORGIX_BSP_CONSOLE_H
#define FORGIX_BSP_CONSOLE_H

#include "bsp_types.h"

/* Returned by BSP_ConsoleGetCharTimeoutUs in place of a received byte when the
   read window elapses with nothing on the wire. Negative so it can never be
   confused with a real byte value, which is always in 0..255. */
#define BSP_CONSOLE_TIMEOUT ( (int16_t) -1 )

void BSP_ConsoleInit( void );
int16_t BSP_ConsoleGetCharTimeoutUs( const uint32_t timeout_us );
int16_t BSP_ConsolePutChar( const uint8_t character );
int32_t BSP_ConsolePrintf( const char *const format, ... );
int32_t BSP_ConsolePuts( const char *const text );

#endif
