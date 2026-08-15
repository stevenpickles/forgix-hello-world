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
#define BSP_FPGA_DESIGN_ID ( (uint8_t) 0xb8u )




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

/* True after a successful bring-up and while no runtime failure has been
   reported since. Read from a latch, not the bus, so the foreground loop can
   ask every pass. */
bool BSP_FpgaIsReady( void );

/* The diagnostics layer's channel for reporting that the FPGA stopped
   answering its runtime health check. Clears readiness; only a successful
   reconfiguration or bring-up sets it again. */
void BSP_FpgaMarkUnresponsive( void );

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

/* One-bit general-purpose FPGA output on board-edge PIN13 / Trion ball F5.
   This is distinct from RP2354 GPIO13, which is never driven. */
void BSP_FpgaGpoSet( const bool high );

bool BSP_FpgaGpoGet( void );

/* Latches the FPGA's free-running 32 MHz counter and returns the snapshot.
   The register addresses stay private to the BSP; callers get one coherent
   32-bit sample per call and time it however they need to. */
uint32_t BSP_FpgaTickSample( void );

#ifdef __cplusplus
}
#endif

#endif
