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
static char _output[ 2048 ];
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


void MOCK_BSP_ConsoleReset( void )
{
    _output[ 0 ] = 0;
    _inputCount = 0;
    _inputPosition = 0;
}


void MOCK_BSP_ConsoleQueueCharacter( const uint8_t character )
{
    if ( _inputCount < (uint32_t) ( sizeof _input / sizeof _input[ 0 ] ) )
    {
        _input[ _inputCount++ ] = character;
    }
}


void MOCK_BSP_ConsoleQueueText( const char *ptr_text )
{
    while ( *ptr_text )
    {
        MOCK_BSP_ConsoleQueueCharacter( (uint8_t) *ptr_text++ );
    }
}


const char *MOCK_BSP_ConsoleOutput( void )
{
    return _output;
}


void BSP_ConsoleInit( void )
{
}


int16_t BSP_ConsoleGetCharTimeoutUs( const uint32_t timeoutUs )
{
    (void) timeoutUs;
    if ( _inputPosition < _inputCount )
    {
        return _input[ _inputPosition++ ];
    }
    return BSP_CONSOLE_TIMEOUT;
}


int16_t BSP_ConsolePutChar( const uint8_t character )
{
    char ptr_text[ 2 ] = { (char) character, 0 };
    _Append( ptr_text );
    return character;
}


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


static void _Append( const char *const ptr_text )
{
    const uint32_t used = (uint32_t) strlen( _output );
    const uint32_t remaining = (uint32_t) sizeof _output - used;
    if ( remaining > 1u )
    {
        snprintf( _output + used, remaining, "%s", ptr_text );
    }
}
