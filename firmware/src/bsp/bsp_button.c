#include "bsp_button.h"

#include "bsp_fpga.h"

enum {
    REG_BUTTON = 0x20,
    REG_BUTTON_COUNT = 0x21,
};

bsp_button_state_t bsp_button_get_state(void) {
    bsp_button_state_t state = {
        .level = bsp_fpga_read_register(REG_BUTTON),
        .count = bsp_fpga_read_register(REG_BUTTON_COUNT),
    };
    return state;
}
