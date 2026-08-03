#ifndef FORGIX_MOCK_BSP_TIME_H
#define FORGIX_MOCK_BSP_TIME_H

#include "bsp_time.h"

void MOCK_BSP_TimeReset(void);
void MOCK_BSP_TimeSetMs(const uint32_t now_ms);

/* Blocking sleeps are recorded rather than performed, so the boot blink code can
   be asserted without slowing the suite. */
uint32_t MOCK_BSP_TimeSleepCount(void);
uint32_t MOCK_BSP_TimeSleepTotalMs(void);

#endif
