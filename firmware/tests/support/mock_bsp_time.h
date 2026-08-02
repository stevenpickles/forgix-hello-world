#ifndef FORGIX_MOCK_BSP_TIME_H
#define FORGIX_MOCK_BSP_TIME_H

#include "bsp_time.h"

void mock_bsp_time_reset(void);
void mock_bsp_time_set_ms(uint32_t now_ms);

#endif
