#include "mock_bsp_console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The queue holds only bytes a host could actually send. BSP_CONSOLE_TIMEOUT is
   produced when the queue runs dry rather than stored in it, so the element type
   does not have to carry the sentinel. */
static char output[ 2048 ];
static uint8_t input[ 512 ];
static uint32_t input_count;
static uint32_t input_position;

static void append( const char *const text )
{
    const uint32_t used = (uint32_t) strlen( output );
    const uint32_t remaining = (uint32_t) sizeof output - used;
    if ( remaining > 1u )
    {
        snprintf( output + used, remaining, "%s", text );
    }
}

void MOCK_BSP_ConsoleReset( void )
{
    output[ 0 ] = 0;
    input_count = 0;
    input_position = 0;
}

void MOCK_BSP_ConsoleQueueCharacter( const uint8_t character )
{
    if ( input_count < (uint32_t) ( sizeof input / sizeof input[ 0 ] ) )
    {
        input[ input_count++ ] = character;
    }
}

void MOCK_BSP_ConsoleQueueText( const char *text )
{
    while ( *text )
    {
        MOCK_BSP_ConsoleQueueCharacter( (uint8_t) *text++ );
    }
}

const char *MOCK_BSP_ConsoleOutput( void )
{
    return output;
}

void BSP_ConsoleInit( void )
{
}

int16_t BSP_ConsoleGetCharTimeoutUs( const uint32_t timeout_us )
{
    (void) timeout_us;
    if ( input_position < input_count )
    {
        return input[ input_position++ ];
    }
    return BSP_CONSOLE_TIMEOUT;
}

int16_t BSP_ConsolePutChar( const uint8_t character )
{
    char text[ 2 ] = { (char) character, 0 };
    append( text );
    return character;
}

int32_t BSP_ConsolePrintf( const char *const format, ... )
{
    char formatted[ 512 ];
    va_list arguments;
    va_start( arguments, format );
    int result = vsnprintf( formatted, sizeof formatted, format, arguments );
    va_end( arguments );
    append( formatted );
    return (int32_t) result;
}

int32_t BSP_ConsolePuts( const char *const text )
{
    append( text );
    append( "\n" );
    return 0;
}
