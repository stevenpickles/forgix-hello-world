#include "mock_bsp_time.h"

static uint32_t current_time_ms;

void mock_bsp_time_reset(void) {
    current_time_ms = 0;
}

void mock_bsp_time_set_ms(uint32_t now_ms) {
    current_time_ms = now_ms;
}

uint32_t bsp_time_now_ms(void) {
    return current_time_ms;
}
