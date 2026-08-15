#ifndef FORGIX_BSP_MEMORY_BIST_H
#define FORGIX_BSP_MEMORY_BIST_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_memory_bist_logic.h"
#include "bsp_types.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


/* One step's worth of progress. pair and verifying describe the sweep the
   step just worked on; sweep_boundary marks the step that completed it, which
   is the caller's cue to print a progress line -- once per sweep keeps a
   watching serial harness fed without flooding the console. The log rides
   along every step so an aborted run still hands over whatever it found. */
typedef struct bsp_memory_bist_status_t_tag
{
    bool done; /* the run has ended; result and restored are meaningful */
    bsp_memory_bist_result result;
    bool restored; /* whether the QPI window came back after the run */
    uint32_t pair;
    bool verifying;
    bool sweep_boundary;
    uint32_t bytes_done; /* bus bytes completed, writes plus verifies */
    uint32_t error_total;
    bsp_memory_bist_log_t log;
} bsp_memory_bist_status_t;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


/* The destructive runtime memory test, sliced for the foreground loop. Each
   Step moves 4 KiB through the ordinary uncached QPI mapping with interrupts
   and flash XIP left running. The test owns the whole device while active;
   completion leaves it cleared. Read-ID and QMI direct mode are boot-POST-only. */
void BSP_MemoryBistStart( void );

bsp_memory_bist_status_t BSP_MemoryBistStep( void );

/* Stops between slices and reports whether the mapped 2 MiB window is still
   advertised. No mode restoration is needed because BIST never changes it. */
bool BSP_MemoryBistAbort( void );

#ifdef __cplusplus
}
#endif

#endif
