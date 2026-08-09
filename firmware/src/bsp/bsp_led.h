#ifndef FORGIX_BSP_LED_H
#define FORGIX_BSP_LED_H

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


typedef struct bsp_led_state_t_tag
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    bool enabled;
} bsp_led_state_t;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


void BSP_LedSet( const uint8_t red, const uint8_t green, const uint8_t blue,
                 const uint8_t brightness );

void BSP_LedOff( void );

bsp_led_state_t BSP_LedGet( void );

/* Puts back everything BSP_LedGet captured, including whether the LED was lit.
   BSP_LedSet cannot do this: it always asserts the enable bit, so a state saved
   dark would come back lit. Not atomic -- five bus transactions. */
void BSP_LedRestore( const bsp_led_state_t *const ptr_state );

#ifdef __cplusplus
}
#endif

#endif
