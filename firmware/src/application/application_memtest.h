#ifndef FORGIX_APPLICATION_MEMTEST_H
#define FORGIX_APPLICATION_MEMTEST_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_ui.h"




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


/* The destructive mapped-QPI memory test as a menu activity: twenty-three
   pattern pairs over the whole 2 MiByte device, one bus slice per foreground
   pass, a progress line per completed sweep, and the standard any-key abort
   against the uncached window. Reached from menu key 7 and `memtest`. */
const application_activity_t *application_memtest_activity( void );

#ifdef __cplusplus
}
#endif

#endif
