/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "mock_bsp_console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* The queue holds only bytes a host could actually send. BSP_CONSOLE_TIMEOUT is
   produced when the queue runs dry rather than stored in it, so the element type
   does not have to carry the sentinel. */
/* Sized for a whole built-in test run. Fourteen result lines, their headings and
   the summary run past two kilobytes, and a capture that truncates turns a
   passing tail assertion into a confusing failure about the wrong thing. */
static char _output[ 8192 ];
static uint8_t _input[ 512 ];
static uint32_t _inputCount;
static uint32_t _inputPosition;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static void _Append( const char *const ptr_text );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Empties both the captured output and the queued input, so one test's
///     unconsumed keystrokes cannot be read by the next.
/// </summary>
void MOCK_BSP_ConsoleReset( void )
{
    _output[ 0 ] = 0;
    _inputCount = 0;
    _inputPosition = 0;
}


/// <summary>
///     Stages one byte for the code under test to receive. Silently drops on a
///     full queue rather than failing, so a test that over-queues fails on the
///     assertion it cares about instead of here.
/// </summary>
void MOCK_BSP_ConsoleQueueCharacter( const uint8_t character )
{
    if ( _inputCount < (uint32_t) ( sizeof _input / sizeof _input[ 0 ] ) )
    {
        _input[ _inputCount++ ] = character;
    }
}


/// <summary>
///     Stages a whole string a byte at a time. The terminator is not queued, so
///     the code under test sees exactly the characters a person would type.
/// </summary>
void MOCK_BSP_ConsoleQueueText( const char *ptr_text )
{
    while ( *ptr_text )
    {
        MOCK_BSP_ConsoleQueueCharacter( (uint8_t) *ptr_text++ );
    }
}


/// <summary>
///     Everything written since the last reset, concatenated. Points at the fake's
///     own buffer, so it is only valid until the next reset.
/// </summary>
/// <returns>
///     The accumulated console output.
/// </returns>
const char *MOCK_BSP_ConsoleOutput( void )
{
    return _output;
}


/// <summary>
///     Nothing to initialise. Present because the code under test calls it, and
///     its absence would be a link error rather than a test failure.
/// </summary>
void BSP_ConsoleInit( void )
{
}


/// <summary>
///     Serves the next queued byte, ignoring the timeout entirely -- the queue is
///     either primed or it is not, so no test ever waits.
/// </summary>
/// <returns>
///     The next queued byte, or BSP_CONSOLE_TIMEOUT once the queue is empty.
/// </returns>
int16_t BSP_ConsoleGetCharTimeoutUs( const uint32_t timeoutUs )
{
    (void) timeoutUs;
    if ( _inputPosition < _inputCount )
    {
        return _input[ _inputPosition++ ];
    }
    return BSP_CONSOLE_TIMEOUT;
}


/// <summary>
///     Appends one byte to the captured output.
/// </summary>
/// <returns>
///     The byte, echoed as the real console does.
/// </returns>
int16_t BSP_ConsolePutChar( const uint8_t character )
{
    char ptr_text[ 2 ] = { (char) character, 0 };
    _Append( ptr_text );
    return character;
}


/// <summary>
///     Formats into a fixed buffer before capturing, so a format expanding past
///     512 bytes is truncated here where the real console would not truncate.
/// </summary>
/// <returns>
///     Characters the formatting produced, which may exceed what was captured.
/// </returns>
int32_t BSP_ConsolePrintf( const char *const ptr_format, ... )
{
    char formatted[ 512 ];
    va_list arguments;
    va_start( arguments, ptr_format );
    int result = vsnprintf( formatted, sizeof formatted, ptr_format, arguments );
    va_end( arguments );
    _Append( formatted );
    return (int32_t) result;
}


/// <summary>
///     Captures the line and the newline separately, matching how the real
///     implementation appends it.
/// </summary>
/// <returns>
///     Zero, always: the fake has no failure mode.
/// </returns>
int32_t BSP_ConsolePuts( const char *const ptr_text )
{
    _Append( ptr_text );
    _Append( "\n" );
    return 0;
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/// <summary>
///     Concatenates into the capture buffer, stopping at its end rather than
///     wrapping. A test that overruns sees truncated output, not corruption.
/// </summary>
static void _Append( const char *const ptr_text )
{
    const uint32_t used = (uint32_t) strlen( _output );
    const uint32_t remaining = (uint32_t) sizeof _output - used;
    if ( remaining > 1u )
    {
        snprintf( _output + used, remaining, "%s", ptr_text );
    }
}
