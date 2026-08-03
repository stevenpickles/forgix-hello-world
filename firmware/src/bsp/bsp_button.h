#ifndef FORGIX_BSP_BUTTON_H
#define FORGIX_BSP_BUTTON_H

#include "bsp_types.h"

typedef struct
{
    uint8_t level;
    uint8_t count;
} bsp_button_state_t;

bsp_button_state_t BSP_ButtonGetState(void);

#endif
