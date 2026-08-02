#include "mock_bsp_watchdog.h"

static bsp_boot_reason_t boot_reason;
static uint32_t marker;
static uint32_t snapshots[BSP_WATCHDOG_SNAPSHOT_SLOTS];
static bool started;
static uint32_t timeout_ms;
static uint32_t feed_count;
static uint32_t marker_history[MOCK_BSP_WATCHDOG_MARKER_HISTORY];
static uint32_t marker_writes;

void mock_bsp_watchdog_reset(void) {
    boot_reason = BSP_BOOT_POWER_ON;
    marker = 0;
    for (uint32_t slot = 0; slot < BSP_WATCHDOG_SNAPSHOT_SLOTS; ++slot) {
        snapshots[slot] = 0;
    }
    started = false;
    timeout_ms = 0;
    feed_count = 0;
    marker_writes = 0;
}

void mock_bsp_watchdog_set_boot_reason(bsp_boot_reason_t reason) {
    boot_reason = reason;
}

void mock_bsp_watchdog_set_retained(uint32_t retained_marker, uint32_t slot0, uint32_t slot1,
                                    uint32_t slot2) {
    marker = retained_marker;
    snapshots[0] = slot0;
    snapshots[1] = slot1;
    snapshots[2] = slot2;
}

bool mock_bsp_watchdog_started(void) {
    return started;
}

uint32_t mock_bsp_watchdog_timeout_ms(void) {
    return timeout_ms;
}

uint32_t mock_bsp_watchdog_feed_count(void) {
    return feed_count;
}

uint32_t mock_bsp_watchdog_marker(void) {
    return marker;
}

uint32_t mock_bsp_watchdog_snapshot(uint32_t slot) {
    return slot < BSP_WATCHDOG_SNAPSHOT_SLOTS ? snapshots[slot] : 0;
}

uint32_t mock_bsp_watchdog_marker_writes(void) {
    return marker_writes;
}

uint32_t mock_bsp_watchdog_marker_at(uint32_t index) {
    return index < marker_writes && index < MOCK_BSP_WATCHDOG_MARKER_HISTORY
               ? marker_history[index]
               : 0;
}

bool mock_bsp_watchdog_marker_was_written(uint32_t wanted) {
    for (uint32_t index = 0; index < marker_writes && index < MOCK_BSP_WATCHDOG_MARKER_HISTORY;
         ++index) {
        if (marker_history[index] == wanted) {
            return true;
        }
    }
    return false;
}

void bsp_watchdog_start(uint32_t requested_timeout_ms) {
    started = true;
    timeout_ms = requested_timeout_ms;
}

void bsp_watchdog_feed(void) {
    ++feed_count;
}

bsp_boot_reason_t bsp_watchdog_boot_reason(void) {
    return boot_reason;
}

void bsp_watchdog_marker_set(uint32_t value) {
    marker = value;
    if (marker_writes < MOCK_BSP_WATCHDOG_MARKER_HISTORY) {
        marker_history[marker_writes] = value;
    }
    ++marker_writes;
}

uint32_t bsp_watchdog_marker_get(void) {
    return marker;
}

void bsp_watchdog_snapshot_set(uint32_t slot, uint32_t value) {
    if (slot < BSP_WATCHDOG_SNAPSHOT_SLOTS) {
        snapshots[slot] = value;
    }
}

uint32_t bsp_watchdog_snapshot_get(uint32_t slot) {
    return slot < BSP_WATCHDOG_SNAPSHOT_SLOTS ? snapshots[slot] : 0;
}
