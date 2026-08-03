#ifndef FORGIX_BSP_TIME_H
#define FORGIX_BSP_TIME_H

#include "bsp_types.h"

uint32_t BSP_TimeNowMs( void );

/* Blocking delay. Only for bounded boot-time sequences that run before the
   watchdog is started; the foreground loop must never block. */
void BSP_TimeSleepMs( const uint32_t duration_ms );

#endif
