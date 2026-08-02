#include "bsp_time.h"

#include "pico/stdlib.h"

uint32_t bsp_time_now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

void bsp_time_sleep_ms(uint32_t duration_ms) {
    sleep_ms(duration_ms);
}
