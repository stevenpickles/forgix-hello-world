#ifndef FORGIX_BSP_FPGA_HEALTH_H
#define FORGIX_BSP_FPGA_HEALTH_H

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


/* The readiness verdict and the latch that carries it between bring-up and the
   foreground loop, kept free of hardware access so the transition rules are
   host-testable. bsp_fpga.c owns the bus and the pins; this module owns only
   the judgement. */

bool BSP_FpgaHealthReadyVerdict( const bool configured, const uint8_t designId );

/* Written by bring-up with its verdict, and by the runtime health check with
   false when the FPGA stops answering. Nothing sets it true except a bring-up
   that actually succeeded. */
void BSP_FpgaHealthSetReady( const bool ready );

bool BSP_FpgaHealthIsReady( void );

#ifdef __cplusplus
}
#endif

#endif
