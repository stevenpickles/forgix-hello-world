#ifndef FORGIX_APPLICATION_DIAGNOSTICS_LED_H
#define FORGIX_APPLICATION_DIAGNOSTICS_LED_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include <stdbool.h>
#include <stdint.h>




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


/* Everything about what the heartbeat LED shows and who is allowed to write it.
   The sampling core calls in twice: once to paint the colour the latest health
   deserves, and once to ask whether the FPGA gave that colour back. The
   readback is the health check's third term, so it has to be reachable from
   there even though it is a statement about the LED. */

void application_diagnostics_apply_led( uint32_t now_ms );

bool application_diagnostics_led_readback_matches( void );

#ifdef __cplusplus
}
#endif

#endif
