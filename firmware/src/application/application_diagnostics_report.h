#ifndef FORGIX_APPLICATION_DIAGNOSTICS_REPORT_H
#define FORGIX_APPLICATION_DIAGNOSTICS_REPORT_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


/* The three ways this firmware says what it found. The boot line replays what
   start captured and is emitted again on demand by `diag`; the live line is the
   USB-free image's per-second liveness proof; the blink code is the same boot
   verdict for a board with no console at all. All three read the shared state
   and none of them changes it. */

void application_diagnostics_report_boot( void );

void application_diagnostics_report_live( void );

void application_diagnostics_report_blink( void );

#ifdef __cplusplus
}
#endif

#endif
