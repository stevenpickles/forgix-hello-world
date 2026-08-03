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


/// <summary>
///     Brings up whichever stdio backends the image was linked with, USB CDC or
///     UART. Returns before a host has attached, so early output can be written
///     into a port nobody is reading yet.
/// </summary>
void BSP_ConsoleInit( void )
{
    stdio_init_all();
}

/// <summary>
///     Waits up to the given window for one byte. Bounded rather than blocking
///     so the foreground loop keeps feeding the watchdog while no one is typing.
/// </summary>
/// <returns>
///     The byte received, or BSP_CONSOLE_TIMEOUT if the window closed empty.
/// </returns>
int16_t BSP_ConsoleGetCharTimeoutUs( const uint32_t timeoutUs )
{
    const int character = getchar_timeout_us( timeoutUs );
    if ( character == PICO_ERROR_TIMEOUT )
    {
        return BSP_CONSOLE_TIMEOUT;
    }
    return (int16_t) character;
}

/// <summary>
///     Writes one byte through the SDK's stdio flush, which is untimed -- this
///     can stall if a host has opened the port and stopped reading. The console
///     layer marks the watchdog before calling it for exactly that reason.
/// </summary>
/// <returns>
///     The byte written, or a negative value if stdio rejected it.
/// </returns>
int16_t BSP_ConsolePutChar( const uint8_t character )
{
    return (int16_t) putchar( character );
}

/// <summary>
///     Formats straight into stdio with no intermediate buffer, so there is no
///     length ceiling of ours to overflow -- but also no bound on how long the
///     write holds the foreground loop. Same stall risk as BSP_ConsolePutChar.
/// </summary>
/// <returns>
///     Characters written, or negative on a formatting error.
/// </returns>
int32_t BSP_ConsolePrintf( const char *const ptr_format, ... )
{
    va_list arguments;
    va_start( arguments, ptr_format );
    const int result = vprintf( ptr_format, arguments );
    va_end( arguments );
    return (int32_t) result;
}

/// <summary>
///     Writes a line and appends the newline itself, so callers do not carry
///     one in their string literals.
/// </summary>
/// <returns>
///     Non-negative on success, negative on error, as the C library reports it.
/// </returns>
int32_t BSP_ConsolePuts( const char *const ptr_text )
{
    return (int32_t) puts( ptr_text );
}
