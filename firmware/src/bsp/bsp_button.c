#include "bsp_button.h"

#include "bsp_fpga.h"

enum
{
    REG_BUTTON = 0x20,
    REG_BUTTON_COUNT = 0x21,
};

bsp_button_state_t BSP_ButtonGetState(void)
{
    const bsp_button_state_t state =
    {
        .level = BSP_FpgaReadRegister(REG_BUTTON),
        .count = BSP_FpgaReadRegister(REG_BUTTON_COUNT),
    };
    return state;
}
