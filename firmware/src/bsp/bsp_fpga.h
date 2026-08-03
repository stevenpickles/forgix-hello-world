#ifndef FORGIX_BSP_FPGA_H
#define FORGIX_BSP_FPGA_H

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
** Compiler Define Directives
**
***************************************************************************************/


/* Identity byte the loaded FPGA design answers a ping with. A mismatch here is
   treated as "this is not the design we expect" rather than a bus fault, since
   the bus itself is clearly working well enough to return something. */
#define BSP_FPGA_DESIGN_ID ( (uint8_t) 0xb5u )




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


typedef struct bsp_fpga_init_result_t_tag
{
    bool configured;
    uint8_t design_id;
    bool ready;
    bool cdone;
    bool status_pin;
} bsp_fpga_init_result_t;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


bsp_fpga_init_result_t BSP_FpgaInit( void );

bool BSP_FpgaIsReady( void );

/* Configuration-done pin. Low at runtime means the FPGA lost its configuration,
   which the diagnostics layer treats as a recoverable hardware fault. */
bool BSP_FpgaCdone( void );

/* Reloads the embedded bitstream and revalidates the design ID. Returns true
   when the FPGA is responding again. */
bool BSP_FpgaReconfigure( void );

/* Whether the image was built to attempt recovery after a runtime FPGA fault.
   Reported as a value rather than a compile switch in the application layer, so
   both policies stay reachable and testable. */
bool BSP_FpgaAutoReconfigureEnabled( void );

uint8_t BSP_FpgaPing( void );

uint8_t BSP_FpgaReadStatus( void );

bool BSP_FpgaStatusPin( void );

void BSP_FpgaReset( void );

uint8_t BSP_FpgaReadRegister( const uint8_t address );

void BSP_FpgaWriteRegister( const uint8_t address, const uint8_t value );

#ifdef __cplusplus
}
#endif

#endif
