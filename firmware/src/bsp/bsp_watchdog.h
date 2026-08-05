#ifndef FORGIX_BSP_WATCHDOG_H
#define FORGIX_BSP_WATCHDOG_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_types.h"




/***************************************************************************************
**
** Compiler Define Directives
**
***************************************************************************************/


/* How many retained 32-bit words the BSP reserves for the diagnostics layer
   in the watchdog scratch registers that survive a reset. Slot indices run
   0..SLOTS-1; also used as the bound of the snapshot arrays in application
   code and the fakes, so it must stay a compile-time constant. */
#define BSP_WATCHDOG_SNAPSHOT_SLOTS ( (uint32_t) 3u )




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


/* Why the last boot ended, as reported by the reset controller. The BSP
   classifies the hardware bits once and latches the answer, because arming
   the watchdog overwrites the flag the classification reads -- so the query
   stays truthful at any point in the run, not only before BSP_WatchdogStart. */
typedef enum bsp_boot_reason_tag
{
    BSP_BOOT_POWER_ON, /* normal power-up from cold */
    BSP_BOOT_BROWNOUT, /* supply dropped out of tolerance and the reset controller reacted */
    BSP_BOOT_WATCHDOG, /* the watchdog timer expired without being fed */
    BSP_BOOT_OTHER,    /* reset cause the BSP does not classify */
} bsp_boot_reason;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


void BSP_WatchdogStart( const uint32_t timeoutMs );

void BSP_WatchdogFeed( void );

bsp_boot_reason BSP_WatchdogBootReason( void );

void BSP_WatchdogMarkerSet( const uint32_t marker );

uint32_t BSP_WatchdogMarkerGet( void );

void BSP_WatchdogSnapshotSet( const uint32_t slot, const uint32_t value );

uint32_t BSP_WatchdogSnapshotGet( const uint32_t slot );

#ifdef __cplusplus
}
#endif

#endif
