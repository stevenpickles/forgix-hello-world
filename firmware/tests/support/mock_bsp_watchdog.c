/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "mock_bsp_watchdog.h"




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


static bsp_boot_reason_t _bootReason;
static uint32_t _marker;
static uint32_t _snapshots[ BSP_WATCHDOG_SNAPSHOT_SLOTS ];
static bool _started;
static uint32_t _timeoutMs;
static uint32_t _feedCount;
static uint32_t _markerHistory[ MOCK_BSP_WATCHDOG_MARKER_HISTORY ];
static uint32_t _markerWrites;




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


void MOCK_BSP_WatchdogReset( void )
{
    _bootReason = BSP_BOOT_POWER_ON;
    _marker = 0;
    for ( uint32_t slot = 0; slot < BSP_WATCHDOG_SNAPSHOT_SLOTS; ++slot )
    {
        _snapshots[ slot ] = 0;
    }
    _started = false;
    _timeoutMs = 0;
    _feedCount = 0;
    _markerWrites = 0;
}


void MOCK_BSP_WatchdogSetBootReason( const bsp_boot_reason_t reason )
{
    _bootReason = reason;
}


void MOCK_BSP_WatchdogSetRetained( const uint32_t retainedMarker, const uint32_t slot0,
                                   const uint32_t slot1, const uint32_t slot2 )
{
    _marker = retainedMarker;
    _snapshots[ 0 ] = slot0;
    _snapshots[ 1 ] = slot1;
    _snapshots[ 2 ] = slot2;
}


bool MOCK_BSP_WatchdogStarted( void )
{
    return _started;
}


uint32_t MOCK_BSP_WatchdogTimeoutMs( void )
{
    return _timeoutMs;
}


uint32_t MOCK_BSP_WatchdogFeedCount( void )
{
    return _feedCount;
}


uint32_t MOCK_BSP_WatchdogMarker( void )
{
    return _marker;
}


uint32_t MOCK_BSP_WatchdogSnapshot( const uint32_t slot )
{
    if ( slot < BSP_WATCHDOG_SNAPSHOT_SLOTS )
    {
        return _snapshots[ slot ];
    }
    return 0;
}


uint32_t MOCK_BSP_WatchdogMarkerWrites( void )
{
    return _markerWrites;
}


uint32_t MOCK_BSP_WatchdogMarkerAt( const uint32_t index )
{
    if ( index < _markerWrites && index < MOCK_BSP_WATCHDOG_MARKER_HISTORY )
    {
        return _markerHistory[ index ];
    }
    return 0;
}


bool MOCK_BSP_WatchdogMarkerWasWritten( const uint32_t wanted )
{
    for ( uint32_t index = 0; index < _markerWrites && index < MOCK_BSP_WATCHDOG_MARKER_HISTORY;
          ++index )
    {
        if ( _markerHistory[ index ] == wanted )
        {
            return true;
        }
    }
    return false;
}


void BSP_WatchdogStart( const uint32_t requestedTimeoutMs )
{
    _started = true;
    _timeoutMs = requestedTimeoutMs;
}


void BSP_WatchdogFeed( void )
{
    ++_feedCount;
}


bsp_boot_reason_t BSP_WatchdogBootReason( void )
{
    return _bootReason;
}


void BSP_WatchdogMarkerSet( const uint32_t value )
{
    _marker = value;
    if ( _markerWrites < MOCK_BSP_WATCHDOG_MARKER_HISTORY )
    {
        _markerHistory[ _markerWrites ] = value;
    }
    ++_markerWrites;
}


uint32_t BSP_WatchdogMarkerGet( void )
{
    return _marker;
}


void BSP_WatchdogSnapshotSet( const uint32_t slot, const uint32_t value )
{
    if ( slot < BSP_WATCHDOG_SNAPSHOT_SLOTS )
    {
        _snapshots[ slot ] = value;
    }
}


uint32_t BSP_WatchdogSnapshotGet( const uint32_t slot )
{
    if ( slot < BSP_WATCHDOG_SNAPSHOT_SLOTS )
    {
        return _snapshots[ slot ];
    }
    return 0;
}
