#ifndef FORGIX_APPLICATION_IBIT_INTERNAL_H
#define FORGIX_APPLICATION_IBIT_INTERNAL_H

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

#include "application_ibit.h"
#include "bsp.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


/* Not part of the module's public surface: this header exists so the runner in
   application_ibit.c and the step implementations in application_ibit_steps_*.c
   can share one state instance without publishing it to the rest of the
   application, which sees only application_ibit.h. */
typedef struct
{
    uint32_t index;
    uint32_t first_index;
    uint32_t last_index;
    bool skipping;
    uint32_t phase;
    uint32_t step_started_ms;
    uint32_t sequence_started_ms;
    uint32_t next_poll_ms;
    uint32_t current_time_ms;
    uint32_t pass;
    uint32_t fail;
    uint32_t timeout;
    uint32_t skip;
    uint32_t info;
    /* BSP_MemoryCheck writes a pattern across the whole PSRAM range, so it is
       sampled once per run and read twice rather than run twice. */
    bool memory_sampled;
    bsp_memory_report_t memory;
    /* The PSRAM step's scratch: chunks per sweep pass, derived from the
       reported size, and this run's legal-window identity read. */
    uint32_t psram_chunks;
    bsp_memory_psram_identity_t psram_identity;
    /* Captured before the LED step drives anything, so whatever the user had
       showing comes back afterwards. */
    bool led_saved;
    bsp_led_state_t led_before;
    uint32_t usb_frame_before;
    uint32_t fpga_tick_before;
    /* The clock step's own t0, stamped in the same pass as the first capture.
       begin_step's stamp is one poll older, and that offset would sit inside
       the measurement for no reason. */
    uint32_t fpga_tick_t0_ms;
    uint8_t button_count_before;
    uint8_t button_level_before;
    bool button_level_moved;
    uint32_t soak_iterations;
    uint32_t soak_failures;
    uint32_t soak_timeouts;
} ibit_state_t;


/* The single instance, defined in application_ibit.c. A step is a foreground
   state machine spanning many passes, so its scratch has to outlive the pass
   that wrote it; the runner is what clears the per-step half between steps. */
extern ibit_state_t ibit;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


/* The helpers more than one of the three modules needs. Anything used by a
   single module stays static in that module; these are the ones the split
   would otherwise have duplicated. */

/* Re-points the retained watchdog marker at the console write about to happen.
   Every module that prints calls it immediately before each write. */
void application_ibit_mark_write( void );

/* Milliseconds since the current step began, judged against the stamp the
   runner cached at the top of this pass. */
uint32_t application_ibit_step_elapsed_ms( void );

/* The one-percent band both clock steps judge their measurement against. */
bool application_ibit_within_tolerance( uint32_t measured, uint32_t expected );

/* Narrows a boolean to the pass/fail pair, which is what a step with no other
   answer to give returns. */
application_ibit_outcome_t application_ibit_verdict( bool ok );

#ifdef __cplusplus
}
#endif

#endif
