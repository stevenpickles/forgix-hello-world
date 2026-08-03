#include "mock_bsp_watchdog.h"

static bsp_boot_reason_t boot_reason;
static uint32_t marker;
static uint32_t snapshots[BSP_WATCHDOG_SNAPSHOT_SLOTS];
static bool started;
static uint32_t timeout_ms;
static uint32_t feed_count;
static uint32_t marker_history[MOCK_BSP_WATCHDOG_MARKER_HISTORY];
static uint32_t marker_writes;

void MOCK_BSP_WatchdogReset(void)
{
    boot_reason = BSP_BOOT_POWER_ON;
    marker = 0;
    for (uint32_t slot = 0; slot < BSP_WATCHDOG_SNAPSHOT_SLOTS; ++slot)
    {
        snapshots[slot] = 0;
    }
    started = false;
    timeout_ms = 0;
    feed_count = 0;
    marker_writes = 0;
}

void MOCK_BSP_WatchdogSetBootReason(const bsp_boot_reason_t reason)
{
    boot_reason = reason;
}

void MOCK_BSP_WatchdogSetRetained(const uint32_t retained_marker, const uint32_t slot0,
                                  const uint32_t slot1, const uint32_t slot2)
{
    marker = retained_marker;
    snapshots[0] = slot0;
    snapshots[1] = slot1;
    snapshots[2] = slot2;
}

bool MOCK_BSP_WatchdogStarted(void)
{
    return started;
}

uint32_t MOCK_BSP_WatchdogTimeoutMs(void)
{
    return timeout_ms;
}

uint32_t MOCK_BSP_WatchdogFeedCount(void)
{
    return feed_count;
}

uint32_t MOCK_BSP_WatchdogMarker(void)
{
    return marker;
}

uint32_t MOCK_BSP_WatchdogSnapshot(const uint32_t slot)
{
    if (slot < BSP_WATCHDOG_SNAPSHOT_SLOTS)
    {
        return snapshots[slot];
    }
    return 0;
}

uint32_t MOCK_BSP_WatchdogMarkerWrites(void)
{
    return marker_writes;
}

uint32_t MOCK_BSP_WatchdogMarkerAt(const uint32_t index)
{
    if (index < marker_writes && index < MOCK_BSP_WATCHDOG_MARKER_HISTORY)
    {
        return marker_history[index];
    }
    return 0;
}

bool MOCK_BSP_WatchdogMarkerWasWritten(const uint32_t wanted)
{
    for (uint32_t index = 0; index < marker_writes && index < MOCK_BSP_WATCHDOG_MARKER_HISTORY;
         ++index)
    {
        if (marker_history[index] == wanted)
        {
            return true;
        }
    }
    return false;
}

void BSP_WatchdogStart(const uint32_t requested_timeout_ms)
{
    started = true;
    timeout_ms = requested_timeout_ms;
}

void BSP_WatchdogFeed(void)
{
    ++feed_count;
}

bsp_boot_reason_t BSP_WatchdogBootReason(void)
{
    return boot_reason;
}

void BSP_WatchdogMarkerSet(const uint32_t value)
{
    marker = value;
    if (marker_writes < MOCK_BSP_WATCHDOG_MARKER_HISTORY)
    {
        marker_history[marker_writes] = value;
    }
    ++marker_writes;
}

uint32_t BSP_WatchdogMarkerGet(void)
{
    return marker;
}

void BSP_WatchdogSnapshotSet(const uint32_t slot, const uint32_t value)
{
    if (slot < BSP_WATCHDOG_SNAPSHOT_SLOTS)
    {
        snapshots[slot] = value;
    }
}

uint32_t BSP_WatchdogSnapshotGet(const uint32_t slot)
{
    if (slot < BSP_WATCHDOG_SNAPSHOT_SLOTS)
    {
        return snapshots[slot];
    }
    return 0;
}
