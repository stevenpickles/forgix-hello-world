#include "bsp.h"

bsp_init_result_t bsp_init(void) {
    bsp_console_init();
    return bsp_fpga_init();
}
