#include "bsp_watchdog.h"

#include "hardware/structs/powman.h"
#include "hardware/watchdog.h"

/* The SDK owns scratch[4..7] for its own reboot bookkeeping, so the diagnostics
   layer is confined to scratch[0] (progress marker) and scratch[1..3]
   (BSP_WATCHDOG_SNAPSHOT_SLOTS health snapshots). Those registers survive a
   watchdog reset, which is what makes a hang self-attributing. */
enum { MARKER_REGISTER = 0, SNAPSHOT_BASE_REGISTER = 1 };

void bsp_watchdog_start(uint32_t timeout_ms) {
    watchdog_enable(timeout_ms, true);
}

void bsp_watchdog_feed(void) {
    watchdog_update();
}

bsp_boot_reason_t bsp_watchdog_boot_reason(void) {
    if (watchdog_enable_caused_reboot()) {
        return BSP_BOOT_WATCHDOG;
    }

    uint32_t chip_reset = powman_hw->chip_reset;
    if (chip_reset & POWMAN_CHIP_RESET_HAD_BOR_BITS) {
        return BSP_BOOT_BROWNOUT;
    }
    if (chip_reset & POWMAN_CHIP_RESET_HAD_POR_BITS) {
        return BSP_BOOT_POWER_ON;
    }
    return BSP_BOOT_OTHER;
}

void bsp_watchdog_marker_set(uint32_t marker) {
    watchdog_hw->scratch[MARKER_REGISTER] = marker;
}

uint32_t bsp_watchdog_marker_get(void) {
    return watchdog_hw->scratch[MARKER_REGISTER];
}

void bsp_watchdog_snapshot_set(uint32_t slot, uint32_t value) {
    if (slot < BSP_WATCHDOG_SNAPSHOT_SLOTS) {
        watchdog_hw->scratch[SNAPSHOT_BASE_REGISTER + slot] = value;
    }
}

uint32_t bsp_watchdog_snapshot_get(uint32_t slot) {
    if (slot < BSP_WATCHDOG_SNAPSHOT_SLOTS) {
        return watchdog_hw->scratch[SNAPSHOT_BASE_REGISTER + slot];
    }
    return 0;
}
