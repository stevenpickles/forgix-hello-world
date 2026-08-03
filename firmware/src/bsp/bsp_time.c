#include "bsp_time.h"

#include "pico/stdlib.h"

uint32_t BSP_TimeNowMs( void )
{
    return to_ms_since_boot( get_absolute_time() );
}

void BSP_TimeSleepMs( const uint32_t duration_ms )
{
    sleep_ms( duration_ms );
}
