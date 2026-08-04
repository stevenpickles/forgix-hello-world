/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_console.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "application.h"
#include "application_diagnostics.h"
#include "bsp.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


enum
{
    COMMAND_CAPACITY = 128
};

/* There is no boot mode here any more. Reporting status once a second until a
   key arrived used to be how the board proved it was alive; the banner the UI
   layer prints before the shell is ever entered does that job now, and does it
   in words a user who has just plugged the board in can act on. */
typedef enum
{
    STATUS_DISABLED,
    STATUS_IDLE,
    STATUS_WATCH,
} status_mode_t;

typedef struct
{
    char line[ COMMAND_CAPACITY ];
    size_t used;
    bool echo_enabled;
    bool quiet;
    bool released;
    bool auto_status_enabled;
    bool swallow_lf;
    status_mode_t status_mode;
    uint32_t current_time_ms;
    uint32_t next_status_ms;
    uint32_t status_period_ms;
} console_state_t;




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


static console_state_t console;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bool deadline_reached( uint32_t now_ms, uint32_t deadline_ms );

static void mark_write( void );

static void print_prompt( void );

static void schedule_idle_status( void );

static void stop_active_status( void );

static void echo_character( int16_t character );

static void erase_character( void );

static void complete_line( void );

static void cancel_line( void );

static void redraw_line( void );

static void process_character( int16_t character );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


void application_console_start( void )
{
    console = (console_state_t) {
        .echo_enabled = true,
        .auto_status_enabled = true,
    };
    console.current_time_ms = BSP_TimeNowMs();
    schedule_idle_status();
    print_prompt();
}


void application_console_feed( int16_t character )
{
    console.current_time_ms = BSP_TimeNowMs();
    process_character( character );
}


void application_console_idle( void )
{
    console.current_time_ms = BSP_TimeNowMs();

    /* Unsolicited output is gated on DTR. Writing status into a port no host has
       opened is the firmware's only unbounded, self-inflicted trip through the
       untimed stdio flush loop. */
    if ( console.quiet || console.used || console.status_mode == STATUS_DISABLED ||
         !deadline_reached( console.current_time_ms, console.next_status_ms ) ||
         !BSP_UsbConnected() )
    {
        return;
    }

    mark_write();
    BSP_ConsolePrintf( "\r\n" );
    application_print_status();
    print_prompt();
    console.next_status_ms = console.current_time_ms + console.status_period_ms;
}


void application_console_release( void )
{
    console.released = true;
    stop_active_status();
}


void application_console_set_echo( bool enabled )
{
    console.echo_enabled = enabled;
}


void application_console_set_quiet( bool enabled )
{
    console.quiet = enabled;
    console.echo_enabled = !enabled;
    console.auto_status_enabled = !enabled;
    stop_active_status();
}


void application_console_set_watch( uint32_t period_seconds )
{
    console.quiet = false;
    console.auto_status_enabled = true;
    console.status_mode = STATUS_WATCH;
    console.status_period_ms = period_seconds * 1000u;
    console.next_status_ms = console.current_time_ms + console.status_period_ms;
}


void application_console_disable_watch( void )
{
    console.auto_status_enabled = false;
    stop_active_status();
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


static bool deadline_reached( uint32_t now_ms, uint32_t deadline_ms )
{
    return (int32_t) ( now_ms - deadline_ms ) >= 0;
}


/* Every console write reaches the untimed Pico SDK stdio flush loop, so the
   marker is set immediately before the call. After a watchdog reset the
   retained marker names the path the foreground was blocked in. */
static void mark_write( void )
{
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE );
}


static void print_prompt( void )
{
    if ( !console.quiet && !console.released )
    {
        mark_write();
        BSP_ConsolePrintf( "forgix> " );
    }
}


static void schedule_idle_status( void )
{
    if ( console.quiet || console.released || !console.auto_status_enabled )
    {
        console.status_mode = STATUS_DISABLED;
        return;
    }

    console.status_mode = STATUS_IDLE;
    console.status_period_ms = APPLICATION_IDLE_STATUS_PERIOD_MS;
    console.next_status_ms = console.current_time_ms + APPLICATION_IDLE_TIMEOUT_MS;
}


static void stop_active_status( void )
{
    console.status_mode = STATUS_DISABLED;
}


static void echo_character( int16_t character )
{
    if ( !console.quiet && console.echo_enabled )
    {
        mark_write();
        BSP_ConsolePutChar( (uint8_t) character );
    }
}


static void erase_character( void )
{
    if ( !console.quiet && console.echo_enabled )
    {
        mark_write();
        BSP_ConsolePrintf( "\b \b" );
    }
}


static void complete_line( void )
{
    if ( !console.quiet && console.echo_enabled )
    {
        mark_write();
        BSP_ConsolePrintf( "\r\n" );
    }

    if ( console.used )
    {
        console.line[ console.used ] = 0;
        BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_COMMAND );
        application_process_command( console.line );
        console.used = 0;
    }

    if ( console.status_mode != STATUS_WATCH )
    {
        schedule_idle_status();
    }
    print_prompt();
}


static void cancel_line( void )
{
    console.used = 0;
    if ( !console.quiet )
    {
        mark_write();
        BSP_ConsolePrintf( "^C\r\n" );
    }
    schedule_idle_status();
    print_prompt();
}


static void redraw_line( void )
{
    if ( !console.quiet )
    {
        mark_write();
        BSP_ConsolePrintf( "\r\nforgix> %.*s", (int) console.used, console.line );
    }
}


static void process_character( int16_t character )
{
    if ( character == '\n' && console.swallow_lf )
    {
        console.swallow_lf = false;
        return;
    }
    console.swallow_lf = false;
    stop_active_status();

    if ( character == '\r' || character == '\n' )
    {
        console.swallow_lf = character == '\r';
        complete_line();
    }
    else if ( character == 3 )
    {
        cancel_line();
    }
    else if ( character == 12 )
    {
        redraw_line();
    }
    else if ( character == 21 )
    {
        while ( console.used )
        {
            --console.used;
            erase_character();
        }
    }
    else if ( character == '\b' || character == 127 )
    {
        if ( console.used )
        {
            --console.used;
            erase_character();
        }
        else
        {
            echo_character( '\a' );
        }
    }
    else if ( isprint( (unsigned char) character ) )
    {
        if ( console.used + 1 < sizeof console.line )
        {
            console.line[ console.used++ ] = (char) character;
            echo_character( character );
        }
        else
        {
            echo_character( '\a' );
        }
    }
}
