#ifndef FORGIX_BSP_WATCHDOG_H
#define FORGIX_BSP_WATCHDOG_H

#include <stdint.h>

enum { BSP_WATCHDOG_SNAPSHOT_SLOTS = 3 };

typedef enum {
    BSP_BOOT_POWER_ON,
    BSP_BOOT_BROWNOUT,
    BSP_BOOT_WATCHDOG,
    BSP_BOOT_OTHER,
} bsp_boot_reason_t;

void bsp_watchdog_start(const uint32_t timeout_ms);
void bsp_watchdog_feed(void);
bsp_boot_reason_t bsp_watchdog_boot_reason(void);

void bsp_watchdog_marker_set(const uint32_t marker);
uint32_t bsp_watchdog_marker_get(void);
void bsp_watchdog_snapshot_set(const uint32_t slot, const uint32_t value);
uint32_t bsp_watchdog_snapshot_get(const uint32_t slot);

#endif
