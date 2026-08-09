#ifndef FORGIX_APPLICATION_IBIT_STEPS_BOARD_H
#define FORGIX_APPLICATION_IBIT_STEPS_BOARD_H

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


/* The steps that need nothing but the MCU and the devices hanging directly off
   it. Each is called once per foreground pass with the buffer the runner will
   print, and answers PENDING for as long as it wants another pass; what it
   carries between passes lives in the shared state application_ibit_internal.h
   declares. The table in application_ibit.c is the only caller, and the order
   the sequence runs them in is that table's business rather than this list's. */

application_ibit_outcome_t application_ibit_step_chip_identity( char *detail, size_t capacity );

application_ibit_outcome_t application_ibit_step_board_identity( char *detail, size_t capacity );

application_ibit_outcome_t application_ibit_step_clocks( char *detail, size_t capacity );

application_ibit_outcome_t application_ibit_step_memory_sizing( char *detail, size_t capacity );

application_ibit_outcome_t application_ibit_step_otp_devinfo( char *detail, size_t capacity );

application_ibit_outcome_t application_ibit_step_boot_flash( char *detail, size_t capacity );

application_ibit_outcome_t application_ibit_step_psram( char *detail, size_t capacity );

application_ibit_outcome_t application_ibit_step_temperature( char *detail, size_t capacity );

application_ibit_outcome_t application_ibit_step_usb( char *detail, size_t capacity );

application_ibit_outcome_t application_ibit_step_watchdog( char *detail, size_t capacity );

#ifdef __cplusplus
}
#endif

#endif
