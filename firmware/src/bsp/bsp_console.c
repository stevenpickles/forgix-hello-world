/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_console.h"

#include "pico/stdlib.h"

#include <stdarg.h>
#include <stdio.h>




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


void BSP_ConsoleInit( void )
{
    stdio_init_all();
}

int16_t BSP_ConsoleGetCharTimeoutUs( const uint32_t timeout_us )
{
    const int character = getchar_timeout_us( timeout_us );
    if ( character == PICO_ERROR_TIMEOUT )
    {
        return BSP_CONSOLE_TIMEOUT;
    }
    return (int16_t) character;
}

int16_t BSP_ConsolePutChar( const uint8_t character )
{
    return (int16_t) putchar( character );
}

int32_t BSP_ConsolePrintf( const char *const format, ... )
{
    va_list arguments;
    va_start( arguments, format );
    const int result = vprintf( format, arguments );
    va_end( arguments );
    return (int32_t) result;
}

int32_t BSP_ConsolePuts( const char *const text )
{
    return (int32_t) puts( text );
}
