/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_time.h"

#include "pico/stdlib.h"




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Milliseconds since boot, from the SDK's 64-bit timer narrowed to 32 bits.
///     Wraps after roughly 49 days, so callers must compare differences rather
///     than absolute values.
/// </summary>
/// <returns>
///     Milliseconds elapsed since power-up.
/// </returns>
uint32_t BSP_TimeNowMs( void )
{
    return to_ms_since_boot( get_absolute_time() );
}


/// <summary>
///     Blocking delay, for bounded boot-time sequences that run before the
///     watchdog is started. The foreground loop must never block: a stall there
///     is indistinguishable from the hang the watchdog exists to catch.
/// </summary>
void BSP_TimeSleepMs( const uint32_t durationMs )
{
    sleep_ms( durationMs );
}
