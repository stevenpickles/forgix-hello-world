#ifndef FORGIX_BSP_H
#define FORGIX_BSP_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_button.h"
#include "bsp_console.h"
#include "bsp_fpga.h"
#include "bsp_led.h"
#include "bsp_memory.h"
#include "bsp_time.h"
#include "bsp_types.h"
#include "bsp_usb.h"
#include "bsp_watchdog.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


typedef bsp_fpga_init_result_t bsp_init_result_t;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


bsp_init_result_t BSP_Init( void );

#ifdef __cplusplus
}
#endif

#endif
