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


void MOCK_BSP_TimeReset( void )
{
    _currentTimeMs = 0;
    _sleepCount = 0;
    _sleepTotalMs = 0;
}


void MOCK_BSP_TimeSetMs( const uint32_t nowMs )
{
    _currentTimeMs = nowMs;
}


uint32_t MOCK_BSP_TimeSleepCount( void )
{
    return _sleepCount;
}


uint32_t MOCK_BSP_TimeSleepTotalMs( void )
{
    return _sleepTotalMs;
}


uint32_t BSP_TimeNowMs( void )
{
    return _currentTimeMs;
}


void BSP_TimeSleepMs( const uint32_t durationMs )
{
    ++_sleepCount;
    _sleepTotalMs += durationMs;
}
