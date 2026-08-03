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


static bsp_boot_reason _bootReason;
static uint32_t _marker;
static uint32_t _snapshots[ BSP_WATCHDOG_SNAPSHOT_SLOTS ];
static bool _started;
static uint32_t _timeoutMs;
static uint32_t _feedCount;
static uint32_t _markerHistory[ MOCK_BSP_WATCHDOG_MARKER_HISTORY ];
static uint32_t _markerWrites;
static bool _markerReadbackFaulty;




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Clears both the retained state and the record of what the code under test
///     did, which are separate concerns: one models what survived a reset, the
///     other is the test's observation of this run.
/// </summary>
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
    _markerReadbackFaulty = false;
}


/// <summary>
///     Poses a scratch register that accepts a write and does not hold it. The
///     built-in test round-trips the marker precisely because that register
///     failing would silently invalidate every watchdog diagnosis.
/// </summary>
void MOCK_BSP_WatchdogSetMarkerReadbackFaulty( const bool faulty )
{
    _markerReadbackFaulty = faulty;
}


/// <summary>
///     Poses the cause of the boot the code is waking from.
/// </summary>
void MOCK_BSP_WatchdogSetBootReason( const bsp_boot_reason reason )
{
    _bootReason = reason;
}


/// <summary>
///     Stages the marker and snapshots as a prior boot would have left them, so
///     the boot report can be tested without actually resetting anything.
/// </summary>
void MOCK_BSP_WatchdogSetRetained( const uint32_t retainedMarker, const uint32_t slot0,
                                   const uint32_t slot1, const uint32_t slot2 )
{
    _marker = retainedMarker;
    _snapshots[ 0 ] = slot0;
    _snapshots[ 1 ] = slot1;
    _snapshots[ 2 ] = slot2;
}


/// <summary>
///     Whether the code under test armed the watchdog. Arming is irreversible on
///     hardware, so tests assert it happens exactly once and at the right point.
/// </summary>
/// <returns>
///     True once BSP_WatchdogStart has been called.
/// </returns>
bool MOCK_BSP_WatchdogStarted( void )
{
    return _started;
}


/// <summary>
///     The window the code asked for, which the real hardware would hold it to.
/// </summary>
/// <returns>
///     The requested timeout, or zero if never armed.
/// </returns>
uint32_t MOCK_BSP_WatchdogTimeoutMs( void )
{
    return _timeoutMs;
}


/// <summary>
///     How many times the loop fed the watchdog. A count rather than a flag,
///     because feeding too often is as much a bug as not feeding at all.
/// </summary>
/// <returns>
///     Feeds since the last reset.
/// </returns>
uint32_t MOCK_BSP_WatchdogFeedCount( void )
{
    return _feedCount;
}


/// <summary>
///     The marker as it stands now, which is the last one written.
/// </summary>
/// <returns>
///     The current marker value.
/// </returns>
uint32_t MOCK_BSP_WatchdogMarker( void )
{
    return _marker;
}


/// <summary>
///     Reads a staged or written snapshot slot, mirroring the real out-of-range
///     behaviour so a test cannot pass against the fake and fail on hardware.
/// </summary>
/// <returns>
///     The slot contents, or zero if the slot is out of range.
/// </returns>
uint32_t MOCK_BSP_WatchdogSnapshot( const uint32_t slot )
{
    if ( slot < BSP_WATCHDOG_SNAPSHOT_SLOTS )
    {
        return _snapshots[ slot ];
    }
    return 0;
}


/// <summary>
///     How many markers were written, counting past the history bound even though
///     only the first few are retained.
/// </summary>
/// <returns>
///     Total marker writes since the last reset.
/// </returns>
uint32_t MOCK_BSP_WatchdogMarkerWrites( void )
{
    return _markerWrites;
}


/// <summary>
///     One entry from the ordered history, which is what lets a test assert that a
///     path was marked before it ran rather than only that the final marker is
///     right.
/// </summary>
/// <returns>
///     The marker at that position, or zero past the end of the record.
/// </returns>
uint32_t MOCK_BSP_WatchdogMarkerAt( const uint32_t index )
{
    if ( index < _markerWrites && index < MOCK_BSP_WATCHDOG_MARKER_HISTORY )
    {
        return _markerHistory[ index ];
    }
    return 0;
}


/// <summary>
///     Whether a marker was set at any point, not just last. Answers "did the code
///     reach here" for a path something else has since moved past.
/// </summary>
/// <returns>
///     True if the marker appears anywhere in the retained history.
/// </returns>
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


/// <summary>
///     Records the arming instead of performing it.
/// </summary>
void BSP_WatchdogStart( const uint32_t requestedTimeoutMs )
{
    _started = true;
    _timeoutMs = requestedTimeoutMs;
}


/// <summary>
///     Counts the feed. Never resets anything, so a test can watch the count grow
///     across a whole run.
/// </summary>
void BSP_WatchdogFeed( void )
{
    ++_feedCount;
}


/// <summary>
///     Returns the staged cause, which never changes on its own.
/// </summary>
/// <returns>
///     The boot reason the test posed.
/// </returns>
bsp_boot_reason BSP_WatchdogBootReason( void )
{
    return _bootReason;
}


/// <summary>
///     Records the marker and appends it to the history, dropping the append once
///     the history is full while still counting the write.
/// </summary>
void BSP_WatchdogMarkerSet( const uint32_t value )
{
    _marker = value;
    if ( _markerWrites < MOCK_BSP_WATCHDOG_MARKER_HISTORY )
    {
        _markerHistory[ _markerWrites ] = value;
    }
    ++_markerWrites;
}


/// <summary>
///     Reads the marker back, staged or written -- the fake does not distinguish
///     them, matching hardware where a retained register reads the same either
///     way.
/// </summary>
/// <returns>
///     The current marker.
/// </returns>
uint32_t BSP_WatchdogMarkerGet( void )
{
    if ( _markerReadbackFaulty )
    {
        return ~_marker;
    }
    return _marker;
}


/// <summary>
///     Writes a snapshot slot, ignoring out-of-range slots exactly as the real
///     implementation does.
/// </summary>
void BSP_WatchdogSnapshotSet( const uint32_t slot, const uint32_t value )
{
    if ( slot < BSP_WATCHDOG_SNAPSHOT_SLOTS )
    {
        _snapshots[ slot ] = value;
    }
}


/// <summary>
///     Reads a snapshot slot.
/// </summary>
/// <returns>
///     The slot contents, or zero if out of range.
/// </returns>
uint32_t BSP_WatchdogSnapshotGet( const uint32_t slot )
{
    if ( slot < BSP_WATCHDOG_SNAPSHOT_SLOTS )
    {
        return _snapshots[ slot ];
    }
    return 0;
}
