#ifndef FORGIX_MOCK_BSP_WATCHDOG_H
#define FORGIX_MOCK_BSP_WATCHDOG_H


#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_types.h"
#include "bsp_watchdog.h"




/***************************************************************************************
**
** Compiler Define Directives
**
***************************************************************************************/


/* How many marker writes the fake records, so a test can assert the order a
   code path set them in rather than only the final value. */
#define MOCK_BSP_WATCHDOG_MARKER_HISTORY ( (uint32_t) 64u )




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


void MOCK_BSP_WatchdogReset( void );

/* Retained state a reset would have left behind, read by the boot report. */
void MOCK_BSP_WatchdogSetBootReason( const bsp_boot_reason reason );

void MOCK_BSP_WatchdogSetRetained( const uint32_t marker, const uint32_t slot0,
                                   const uint32_t slot1, const uint32_t slot2 );

bool MOCK_BSP_WatchdogStarted( void );

uint32_t MOCK_BSP_WatchdogTimeoutMs( void );

uint32_t MOCK_BSP_WatchdogFeedCount( void );

uint32_t MOCK_BSP_WatchdogMarker( void );

uint32_t MOCK_BSP_WatchdogSnapshot( const uint32_t slot );

/* Ordered record of every marker write, so tests can assert that a code path was
   marked before it ran rather than only that the final marker is correct. */
uint32_t MOCK_BSP_WatchdogMarkerWrites( void );

uint32_t MOCK_BSP_WatchdogMarkerAt( const uint32_t index );

bool MOCK_BSP_WatchdogMarkerWasWritten( const uint32_t marker );




#ifdef __cplusplus
}
#endif


#endif
