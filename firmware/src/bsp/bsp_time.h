#ifndef FORGIX_BSP_TIME_H
#define FORGIX_BSP_TIME_H

#include <stdint.h>

uint32_t bsp_time_now_ms(void);

/* Blocking delay. Only for bounded boot-time sequences that run before the
   watchdog is started; the foreground loop must never block. */
void bsp_time_sleep_ms(uint32_t duration_ms);

#endif
