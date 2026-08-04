#ifndef FORGIX_APPLICATION_IBIT_H
#define FORGIX_APPLICATION_IBIT_H

#include <stdbool.h>
#include <stdint.h>

#include "application_ui.h"

enum
{
    /* Long enough that a user who looked away still has time to react, short
       enough that an unattended board finishes the sequence on its own. */
    APPLICATION_IBIT_BUTTON_TIMEOUT_MS = 15000,
};

/* PENDING is not a result, it is a step saying it needs another pass. Everything
   else is counted in the tally.

   TIMEOUT is kept apart from FAIL on purpose: "nobody pressed the button" and
   "the button is broken" produce the same silence, and reporting them as the
   same thing would turn every unattended run into a failing one. SKIP likewise
   -- once the FPGA is unreachable, the LED and button steps cannot mean
   anything, and calling them failures would bury the one fault that is real. */
typedef enum
{
    APPLICATION_IBIT_PENDING,
    APPLICATION_IBIT_PASS,
    APPLICATION_IBIT_FAIL,
    APPLICATION_IBIT_TIMEOUT,
    APPLICATION_IBIT_SKIP,
    APPLICATION_IBIT_INFO,
} application_ibit_outcome_t;

uint32_t application_ibit_step_count( void );
const char *application_ibit_step_name( uint32_t index );

/* The whole sequence, once. */
const application_activity_t *application_ibit_sequence( void );

/* The whole sequence on repeat, with a pass/fail tally across iterations, until
   a key stops it. */
const application_activity_t *application_ibit_soak( void );

/* One step on its own, so a failure can be repeated without sitting through the
   thirteen steps that already passed. */
const application_activity_t *application_ibit_single( uint32_t index );

void application_ibit_print_board_report( void );

#endif
