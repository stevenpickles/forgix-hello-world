#ifndef FORGIX_BSP_CLOCKS_H
#define FORGIX_BSP_CLOCKS_H

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


typedef struct bsp_clocks_report_t_tag
{
    /* What the SDK configured each domain to. These are bookkeeping: they say
       what was asked for, not what the silicon is doing. */
    uint32_t sys_hz;
    uint32_t usb_hz;
    uint32_t ref_hz;
    uint32_t peri_hz;
    uint32_t adc_hz;
    /* What the on-chip frequency counter measured, gated against clk_ref and so
       ultimately against the crystal. A PLL that failed to lock shows up here
       and nowhere else, because clock_get_hz would still report the target. */
    uint32_t measured_sys_hz;
    uint32_t measured_usb_hz;
} bsp_clocks_report_t;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


bsp_clocks_report_t BSP_ClocksReport( void );

#ifdef __cplusplus
}
#endif

#endif
