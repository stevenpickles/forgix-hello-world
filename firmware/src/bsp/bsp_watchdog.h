#ifndef FORGIX_BSP_WATCHDOG_H
#define FORGIX_BSP_WATCHDOG_H

#include <stdint.h>

enum
{
    BSP_WATCHDOG_SNAPSHOT_SLOTS = 3
};

typedef enum
{
    BSP_BOOT_POWER_ON,
    BSP_BOOT_BROWNOUT,
    BSP_BOOT_WATCHDOG,
    BSP_BOOT_OTHER,
} bsp_boot_reason_t;

void BSP_WatchdogStart(const uint32_t timeout_ms);
void BSP_WatchdogFeed(void);
bsp_boot_reason_t BSP_WatchdogBootReason(void);

void BSP_WatchdogMarkerSet(const uint32_t marker);
uint32_t BSP_WatchdogMarkerGet(void);
void BSP_WatchdogSnapshotSet(const uint32_t slot, const uint32_t value);
uint32_t BSP_WatchdogSnapshotGet(const uint32_t slot);

#endif
