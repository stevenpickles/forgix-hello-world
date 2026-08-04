/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "application_console.h"
#include "application_diagnostics.h"
#include "application_ui.h"
#include "bsp.h"




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bool parse_byte( const char *text, uint8_t *value );

static bool parse_watch_period( const char *text, uint32_t *seconds );

static void print_help( void );

static void print_memory_report( void );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


void application_print_status( void )
{
    if ( !BSP_FpgaIsReady() )
    {
        BSP_ConsolePuts( "status unavailable: FPGA is not configured and responding" );
        return;
    }

    bsp_button_state_t button = BSP_ButtonGetState();
    BSP_ConsolePrintf( "id=%02X status=%02X button=%02X count=%u fpga_status=%u\n", BSP_FpgaPing(),
                       BSP_FpgaReadStatus(), button.level, button.count, BSP_FpgaStatusPin() );
}


void application_process_command( char *line )
{
    char *argv[ 6 ] = { 0 };
    int argc = 0;
    for ( char *part = strtok( line, " \t" ); part && argc < 6; part = strtok( NULL, " \t" ) )
    {
        argv[ argc++ ] = part;
    }

    if ( !argc )
    {
        return;
    }
    if ( !strcmp( argv[ 0 ], "help" ) && argc == 1 )
    {
        print_help();
        return;
    }
    if ( !strcmp( argv[ 0 ], "quiet" ) )
    {
        if ( argc == 1 )
        {
            BSP_ConsolePuts( "ok" );
            application_console_set_quiet( true );
        }
        else
        {
            BSP_ConsolePuts( "error: invalid command (try help)" );
        }
        return;
    }
    if ( !strcmp( argv[ 0 ], "interactive" ) )
    {
        if ( argc == 1 )
        {
            application_console_set_quiet( false );
            BSP_ConsolePuts( "ok" );
        }
        else
        {
            BSP_ConsolePuts( "error: invalid command (try help)" );
        }
        return;
    }
    if ( !strcmp( argv[ 0 ], "echo" ) )
    {
        if ( argc == 2 && ( !strcmp( argv[ 1 ], "on" ) || !strcmp( argv[ 1 ], "off" ) ) )
        {
            application_console_set_echo( !strcmp( argv[ 1 ], "on" ) );
            BSP_ConsolePuts( "ok" );
        }
        else
        {
            BSP_ConsolePuts( "error: usage: echo <on|off>" );
        }
        return;
    }
    if ( !strcmp( argv[ 0 ], "watch" ) )
    {
        uint32_t period_seconds = 0;
        if ( argc == 2 && !strcmp( argv[ 1 ], "off" ) )
        {
            application_console_disable_watch();
            BSP_ConsolePuts( "ok" );
        }
        else if ( argc == 2 && parse_watch_period( argv[ 1 ], &period_seconds ) )
        {
            application_console_set_watch( period_seconds );
            BSP_ConsolePuts( "ok" );
        }
        else
        {
            BSP_ConsolePrintf( "error: usage: watch <%u..%u seconds|off>\n",
                               APPLICATION_WATCH_MIN_SECONDS, APPLICATION_WATCH_MAX_SECONDS );
        }
        return;
    }
    if ( !strcmp( argv[ 0 ], "status" ) && argc == 1 )
    {
        application_print_status();
        return;
    }
    if ( !strcmp( argv[ 0 ], "diag" ) && argc == 1 )
    {
        print_memory_report();
        application_diagnostics_print_report();
        return;
    }
    /* Above the FPGA gate: getting back to the menu is how a user reaches the
       tests that diagnose a dead FPGA, so it cannot be one of the things a dead
       FPGA takes away. */
    if ( !strcmp( argv[ 0 ], "menu" ) && argc == 1 )
    {
        application_ui_enter_menu();
        return;
    }
    if ( !BSP_FpgaIsReady() )
    {
        BSP_ConsolePuts( "error: FPGA is not configured and responding; reset the board to retry" );
        return;
    }

    if ( !strcmp( argv[ 0 ], "hello" ) && argc == 1 )
    {
        BSP_LedSet( 0, 255, 255, 64 );
        uint8_t id = BSP_FpgaPing();
        bsp_led_state_t led = BSP_LedGet();
        if ( id == BSP_FPGA_DESIGN_ID && led.red == 0 && led.green == 255 && led.blue == 255 &&
             led.brightness == 64 && led.enabled )
        {
            BSP_ConsolePrintf( "Hello from RP2354 -> FPGA %02X\n", id );
        }
        else
        {
            BSP_ConsolePrintf(
                "error: hello readback failed: id=%02X rgb=%u,%u,%u brightness=%u enable=%u\n", id,
                led.red, led.green, led.blue, led.brightness, led.enabled );
        }
    }
    else if ( !strcmp( argv[ 0 ], "color" ) && ( argc == 4 || argc == 5 ) )
    {
        uint8_t values[ 4 ] = { 0, 0, 0, 255 };
        bool valid = true;
        for ( int index = 1; index < argc; ++index )
        {
            valid &= parse_byte( argv[ index ], &values[ index - 1 ] );
        }
        if ( !valid )
        {
            BSP_ConsolePuts( "error: values must be 0..255" );
            return;
        }
        BSP_LedSet( values[ 0 ], values[ 1 ], values[ 2 ], values[ 3 ] );
        BSP_ConsolePuts( "ok" );
    }
    else if ( !strcmp( argv[ 0 ], "off" ) && argc == 1 )
    {
        BSP_LedOff();
        BSP_ConsolePuts( "ok" );
    }
    else if ( !strcmp( argv[ 0 ], "reset" ) && argc == 1 )
    {
        BSP_FpgaReset();
        BSP_ConsolePuts( "ok" );
    }
    else
    {
        BSP_ConsolePuts( "error: invalid command (try help)" );
    }
}


void application_init( const bsp_init_result_t *bsp_result )
{
    print_memory_report();
    BSP_ConsolePrintf( "Forgix: configuration=%s design_id=%02X runtime=%s cdone=%u status=%u\n",
                       bsp_result->configured ? "ok" : "failed", bsp_result->design_id,
                       bsp_result->ready ? "ready" : "unavailable", bsp_result->cdone,
                       bsp_result->status_pin );
    if ( !bsp_result->ready )
    {
        BSP_ConsolePuts( "error: FPGA configuration or design-ID validation failed; runtime "
                         "commands are disabled" );
    }
    print_help();
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


static bool parse_byte( const char *text, uint8_t *value )
{
    char *end = NULL;
    long parsed = strtol( text, &end, 0 );
    if ( end == text || *end )
    {
        return false;
    }
    if ( parsed < 0 || parsed > 255 )
    {
        return false;
    }
    *value = (uint8_t) parsed;
    return true;
}


static bool parse_watch_period( const char *text, uint32_t *seconds )
{
    char *end = NULL;
    long parsed = strtol( text, &end, 10 );
    if ( end == text || *end || parsed < APPLICATION_WATCH_MIN_SECONDS ||
         parsed > APPLICATION_WATCH_MAX_SECONDS )
    {
        return false;
    }
    *seconds = (uint32_t) parsed;
    return true;
}


static void print_help( void )
{
    BSP_ConsolePuts( "hello | color <r> <g> <b> [brightness] | off | status | diag | menu | reset "
                     "| echo <on|off> | watch <seconds|off> | quiet | interactive | help" );
}


/* Both QSPI memories share the same data lines, so a fault on one shows up as
   the other misbehaving. Reported at boot and repeatable through `diag`, because
   a line that only appears once is a line nobody is listening for. */
static void print_memory_report( void )
{
    bsp_memory_report_t memory = BSP_MemoryCheck();
    BSP_ConsolePrintf(
        "Forgix: flash=%luKiB ok=%u psram=%luKiB ok=%u forced=%u kgd=%02X eid=%02X\n",
        (unsigned long) ( memory.flash_bytes / 1024u ), memory.flash_ok,
        (unsigned long) ( memory.psram_bytes / 1024u ), memory.psram_ok, memory.psram_forced,
        memory.psram_kgd, memory.psram_eid );
}
