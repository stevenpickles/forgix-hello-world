#ifndef FORGIX_BSP_TIME_H
#define FORGIX_BSP_TIME_H

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
** Public Function Declarations
**
***************************************************************************************/


uint32_t BSP_TimeNowMs( void );

void BSP_TimeSleepMs( const uint32_t duration_ms );

#ifdef __cplusplus
}
#endif

#endif
