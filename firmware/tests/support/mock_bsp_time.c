#include "mock_bsp_time.h"

static uint32_t current_time_ms;
static uint32_t sleep_count;
static uint32_t sleep_total_ms;

void mock_bsp_time_reset(void) {
    current_time_ms = 0;
    sleep_count = 0;
    sleep_total_ms = 0;
}

void mock_bsp_time_set_ms(uint32_t now_ms) {
    current_time_ms = now_ms;
}

uint32_t mock_bsp_time_sleep_count(void) {
    return sleep_count;
}

uint32_t mock_bsp_time_sleep_total_ms(void) {
    return sleep_total_ms;
}

uint32_t bsp_time_now_ms(void) {
    return current_time_ms;
}

void bsp_time_sleep_ms(uint32_t duration_ms) {
    ++sleep_count;
    sleep_total_ms += duration_ms;
}
