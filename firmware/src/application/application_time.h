#ifndef FORGIX_APPLICATION_TIME_H
#define FORGIX_APPLICATION_TIME_H

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


/* The wrap-safe signed-difference time comparisons, in one place. Five modules
   used to carry an identical private copy of the deadline test each; a sixth
   copy getting the cast wrong would have re-introduced the 49-day stall the
   idiom exists to prevent. */

bool application_deadline_reached( const uint32_t now_ms, const uint32_t deadline_ms );

bool application_stalled_since( const uint32_t now_ms, const uint32_t since_ms,
                                const uint32_t threshold_ms );

#ifdef __cplusplus
}
#endif

#endif
