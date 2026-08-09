#ifndef FORGIX_APPLICATION_IBIT_STEPS_FPGA_H
#define FORGIX_APPLICATION_IBIT_STEPS_FPGA_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include <stddef.h>

#include "application_ibit.h"




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


/* The steps reached through the FPGA's register bus, and the one that decides
   whether that bus is answering at all. They have the same per-pass contract as
   the board steps: called with the buffer the runner will print, PENDING until
   they have a verdict, scratch in the shared state. All but the configuration
   step are marked in the runner's table as standing down when the FPGA is
   unreachable, so a caller that ran them anyway would be describing the bus
   rather than the part named on the line. */

application_ibit_outcome_t application_ibit_step_fpga_configuration( char *detail,
                                                                     size_t capacity );

application_ibit_outcome_t application_ibit_step_fpga_registers( char *detail, size_t capacity );

application_ibit_outcome_t application_ibit_step_led( char *detail, size_t capacity );

application_ibit_outcome_t application_ibit_step_button( char *detail, size_t capacity );

application_ibit_outcome_t application_ibit_step_fpga_clock( char *detail, size_t capacity );

#ifdef __cplusplus
}
#endif

#endif
