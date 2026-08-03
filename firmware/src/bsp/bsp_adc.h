#ifndef FORGIX_BSP_ADC_H
#define FORGIX_BSP_ADC_H

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


typedef struct bsp_adc_temperature_t_tag
{
    uint16_t raw;          /* 12-bit conversion result, reported so a rail fault is visible */
    int32_t milli_celsius; /* converted with the RP2350 datasheet transfer function */
} bsp_adc_temperature_t;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


void BSP_AdcInit( void );

bsp_adc_temperature_t BSP_AdcTemperature( void );

#ifdef __cplusplus
}
#endif

#endif
