#include "mock_bsp_time.h"

static uint32_t current_time_ms;
static uint32_t sleep_count;
static uint32_t sleep_total_ms;

void MOCK_BSP_TimeReset(void) {
    current_time_ms = 0;
    sleep_count = 0;
    sleep_total_ms = 0;
}

void MOCK_BSP_TimeSetMs(const uint32_t now_ms) {
    current_time_ms = now_ms;
}

uint32_t MOCK_BSP_TimeSleepCount(void) {
    return sleep_count;
}

uint32_t MOCK_BSP_TimeSleepTotalMs(void) {
    return sleep_total_ms;
}

uint32_t BSP_TimeNowMs(void) {
    return current_time_ms;
}

void BSP_TimeSleepMs(const uint32_t duration_ms) {
    ++sleep_count;
    sleep_total_ms += duration_ms;
}
