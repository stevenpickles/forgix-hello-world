#ifndef FORGIX_BSP_BUTTON_H
#define FORGIX_BSP_BUTTON_H

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
** Enumerated Values, Type Definitions
**
***************************************************************************************/


typedef struct bsp_button_state_t_tag
{
    uint8_t level;
    uint8_t count;
} bsp_button_state_t;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


bsp_button_state_t BSP_ButtonGetState( void );

void BSP_ButtonClearCount( void );

#ifdef __cplusplus
}
#endif

#endif
