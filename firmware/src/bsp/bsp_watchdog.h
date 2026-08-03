#ifndef FORGIX_BSP_WATCHDOG_H
#define FORGIX_BSP_WATCHDOG_H

#include "bsp_types.h"

/* How many retained 32-bit words the BSP reserves for the diagnostics layer
   in the watchdog scratch registers that survive a reset. Slot indices run
   0..SLOTS-1; also used as the bound of the snapshot arrays in application
   code and the fakes, so it must stay a compile-time constant. */
#define BSP_WATCHDOG_SNAPSHOT_SLOTS ((uint32_t)3u)

/* Why the last boot ended, as reported by the reset controller. Read once at
   startup so the diagnostics layer can tell a clean power-up apart from a
   watchdog-forced reboot. */
typedef enum
{
    BSP_BOOT_POWER_ON,  /* normal power-up from cold */
    BSP_BOOT_BROWNOUT,  /* supply dropped out of tolerance and the reset controller reacted */
    BSP_BOOT_WATCHDOG,  /* the watchdog timer expired without being fed */
    BSP_BOOT_OTHER,     /* reset cause the BSP does not classify */
} bsp_boot_reason_t;

void BSP_WatchdogStart(const uint32_t timeout_ms);
void BSP_WatchdogFeed(void);
bsp_boot_reason_t BSP_WatchdogBootReason(void);

void BSP_WatchdogMarkerSet(const uint32_t marker);
uint32_t BSP_WatchdogMarkerGet(void);
void BSP_WatchdogSnapshotSet(const uint32_t slot, const uint32_t value);
uint32_t BSP_WatchdogSnapshotGet(const uint32_t slot);

#endif
