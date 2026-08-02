#ifndef FORGIX_MOCK_BSP_TIME_H
#define FORGIX_MOCK_BSP_TIME_H

#include "bsp_time.h"

void mock_bsp_time_reset(void);
void mock_bsp_time_set_ms(uint32_t now_ms);

/* Blocking sleeps are recorded rather than performed, so the boot blink code can
   be asserted without slowing the suite. */
uint32_t mock_bsp_time_sleep_count(void);
uint32_t mock_bsp_time_sleep_total_ms(void);

#endif
