#include "bsp_watchdog.h"

#include "hardware/structs/powman.h"
#include "hardware/watchdog.h"

/* The SDK owns scratch[4..7] for its own reboot bookkeeping, so the diagnostics
   layer is confined to scratch[0] (progress marker) and scratch[1..3]
   (BSP_WATCHDOG_SNAPSHOT_SLOTS health snapshots). Those registers survive a
   watchdog reset, which is what makes a hang self-attributing. */
enum { MARKER_REGISTER = 0, SNAPSHOT_BASE_REGISTER = 1 };

void BSP_WatchdogStart(const uint32_t timeout_ms) {
    watchdog_enable(timeout_ms, true);
}

void BSP_WatchdogFeed(void) {
    watchdog_update();
}

bsp_boot_reason_t BSP_WatchdogBootReason(void) {
    if (watchdog_enable_caused_reboot()) {
        return BSP_BOOT_WATCHDOG;
    }

    const uint32_t chip_reset = powman_hw->chip_reset;
    if (chip_reset & POWMAN_CHIP_RESET_HAD_BOR_BITS) {
        return BSP_BOOT_BROWNOUT;
    }
    if (chip_reset & POWMAN_CHIP_RESET_HAD_POR_BITS) {
        return BSP_BOOT_POWER_ON;
    }
    return BSP_BOOT_OTHER;
}

void BSP_WatchdogMarkerSet(const uint32_t marker) {
    watchdog_hw->scratch[MARKER_REGISTER] = marker;
}

uint32_t BSP_WatchdogMarkerGet(void) {
    return watchdog_hw->scratch[MARKER_REGISTER];
}

void BSP_WatchdogSnapshotSet(const uint32_t slot, const uint32_t value) {
    if (slot < BSP_WATCHDOG_SNAPSHOT_SLOTS) {
        watchdog_hw->scratch[SNAPSHOT_BASE_REGISTER + slot] = value;
    }
}

uint32_t BSP_WatchdogSnapshotGet(const uint32_t slot) {
    if (slot < BSP_WATCHDOG_SNAPSHOT_SLOTS) {
        return watchdog_hw->scratch[SNAPSHOT_BASE_REGISTER + slot];
    }
    return 0;
}
