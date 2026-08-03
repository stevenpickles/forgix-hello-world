/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "mock_bsp_time.h"




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


static uint32_t _currentTimeMs;
static uint32_t _sleepCount;
static uint32_t _sleepTotalMs;




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Returns the clock to zero and forgets recorded sleeps. Tests call this in
///     setUp so one test's elapsed time cannot leak into the next.
/// </summary>
void MOCK_BSP_TimeReset( void )
{
    _currentTimeMs = 0;
    _sleepCount = 0;
    _sleepTotalMs = 0;
}


/// <summary>
///     Places the clock at an absolute instant. Tests step it explicitly rather
///     than letting it run, so a timing-dependent path is exercised at exactly
///     the boundary the test cares about.
/// </summary>
void MOCK_BSP_TimeSetMs( const uint32_t nowMs )
{
    _currentTimeMs = nowMs;
}


/// <summary>
///     How many sleeps were requested. Together with the total this distinguishes
///     one long sleep from several short ones, which the boot blink code depends
///     on.
/// </summary>
/// <returns>
///     Number of BSP_TimeSleepMs calls since the last reset.
/// </returns>
uint32_t MOCK_BSP_TimeSleepCount( void )
{
    return _sleepCount;
}


/// <summary>
///     Milliseconds the code under test asked to sleep for, none of which actually
///     elapsed.
/// </summary>
/// <returns>
///     Summed duration of every sleep request since the last reset.
/// </returns>
uint32_t MOCK_BSP_TimeSleepTotalMs( void )
{
    return _sleepTotalMs;
}


/// <summary>
///     Returns whatever the test last set, and never advances on its own. Time
///     only moves when a test moves it.
/// </summary>
/// <returns>
///     The current fake clock reading.
/// </returns>
uint32_t BSP_TimeNowMs( void )
{
    return _currentTimeMs;
}


/// <summary>
///     Records the request and returns immediately. Sleeping for real would make
///     the boot blink code take its full wall-clock duration in every run.
/// </summary>
void BSP_TimeSleepMs( const uint32_t durationMs )
{
    ++_sleepCount;
    _sleepTotalMs += durationMs;
}
