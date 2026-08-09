#ifndef FORGIX_APPLICATION_CONSOLE_STATUS_H
#define FORGIX_APPLICATION_CONSOLE_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


/* The three verbs the line editor uses to drive the unsolicited-status
   scheduler. Arming and stopping are decisions the editor makes -- a fresh
   shell and a completed line arm, a release or a `quiet` stops -- while pausing
   is what any keystroke does, so a watch goes quiet for the length of a line
   without losing the period it was armed with. The scheduler itself only
   decides when a line is due. */

void application_console_status_schedule_idle( void );

void application_console_status_stop( void );

void application_console_status_pause( void );

#ifdef __cplusplus
}
#endif

#endif
