/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_memtest.h"

#include <stdbool.h>
#include <stdint.h>

#include "application_diagnostics.h"
#include "bsp.h"




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static void mark_write( void );

static void print_progress( const bsp_memory_bist_status_t *ptr_status );

static void print_summary( const bsp_memory_bist_status_t *ptr_status );

static void memtest_start( void );

static bool memtest_poll( void );

static void memtest_stop( void );


/* Names the static handlers above, so it must follow their declarations and
   precede the public function that hands it out. */
static const application_activity_t MEMTEST = {
    .name = "memtest",
    .start = memtest_start,
    .poll = memtest_poll,
    .stop = memtest_stop,
};


/* Indexed by bsp_memory_bist_result, in that enum's order: a table instead of
   a switch so there is no unreachable default branch to keep uncovered. */
static const char *const RESULT_TEXT[] = {
    "pass", "data-fail", "alias-fail", "controller-fail", "skipped",
};




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Hands out a pointer into this file's constant table, so it is always
///     valid and never null.
/// </summary>
/// <returns>
///     The memory-test activity.
/// </returns>
const application_activity_t *application_memtest_activity( void )
{
    return &MEMTEST;
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/* Every console write reaches the untimed Pico SDK stdio flush loop, so the
   marker is set immediately before each call, exactly as the other foreground
   modules do. */
/// <summary>
///     Claims the console-write marker for the line about to be printed.
/// </summary>
static void mark_write( void )
{
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE );
}


/// <summary>
///     One line per completed sweep, enough progress for a watching harness
///     without flooding the terminal.
/// </summary>
static void print_progress( const bsp_memory_bist_status_t *ptr_status )
{
    mark_write();
    BSP_ConsolePrintf(
        "memtest: [%2lu/%2u] %-5s %s done  errors=%lu\n", (unsigned long) ( ptr_status->pair + 1u ),
        (unsigned) BSP_MEMORY_BIST_PAIRS, BSP_MemoryBistPairLabel( ptr_status->pair ),
        ptr_status->verifying ? "verify" : "write", (unsigned long) ptr_status->error_total );
}


/// <summary>
///     The verdict, the recorded fault map, how much of the error population
///     the records cover, and the restore status -- everything a failing run
///     leaves behind, printed once when the run ends however it ends.
/// </summary>
static void print_summary( const bsp_memory_bist_status_t *ptr_status )
{
    if ( ptr_status->result == BSP_MEMORY_BIST_SKIPPED )
    {
        mark_write();
        BSP_ConsolePuts( "memtest: not enabled in this build (FORGIX_QSPI_PSRAM off)" );
        return;
    }

    mark_write();
    BSP_ConsolePrintf( "memtest: %s errors=%lu restored=%u\n", RESULT_TEXT[ ptr_status->result ],
                       (unsigned long) ptr_status->error_total, ptr_status->restored );
    for ( uint32_t index = 0; index < ptr_status->log.recorded; ++index )
    {
        const bsp_memory_bist_error_t *ptr_record = &ptr_status->log.records[ index ];
        mark_write();
        BSP_ConsolePrintf(
            "memtest: err pair=%u(%s) page=%u addr=%06lX expected=%02X actual=%02X xor=%02X\n",
            ptr_record->pair, BSP_MemoryBistPairLabel( ptr_record->pair ), ptr_record->page,
            (unsigned long) ptr_record->address, ptr_record->expected, ptr_record->actual,
            ptr_record->xor_bits );
    }
    if ( ptr_status->log.total > ptr_status->log.recorded )
    {
        mark_write();
        BSP_ConsolePrintf( "memtest: %lu further errors counted beyond the %lu recorded\n",
                           (unsigned long) ( ptr_status->log.total - ptr_status->log.recorded ),
                           (unsigned long) ptr_status->log.recorded );
    }
    if ( ptr_status->result == BSP_MEMORY_BIST_PASS )
    {
        mark_write();
        BSP_ConsolePuts( "memtest: memory left cleared to 0x00" );
    }
}


/// <summary>
///     Announces what is about to happen -- destructive, minutes long, any key
///     aborts -- before the first slice runs, then arms the test. The warning
///     prints from start() so it is on screen before the first poll.
/// </summary>
static void memtest_start( void )
{
    mark_write();
    BSP_ConsolePuts( "memtest: destructive mapped-QPI test of the whole 2 MiB PSRAM; "
                     "23 pattern pairs; any key aborts" );
    BSP_MemoryBistStart();
}


/// <summary>
///     One slice of bus work per foreground pass. Prints only on a sweep
///     boundary or at the end, because a line per slice would be thousands of
///     lines of nothing.
/// </summary>
/// <returns>
///     True while the test still has work; false retires the activity.
/// </returns>
static bool memtest_poll( void )
{
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_MEMTEST );
    const bsp_memory_bist_status_t status = BSP_MemoryBistStep();

    if ( status.sweep_boundary )
    {
        print_progress( &status );
    }
    if ( status.done )
    {
        print_summary( &status );
        return false;
    }
    return true;
}


/// <summary>
///     The any-key abort stops between mapped slices and reports whether the
///     expected window remains advertised.
/// </summary>
static void memtest_stop( void )
{
    const bool restored = BSP_MemoryBistAbort();
    mark_write();
    BSP_ConsolePuts( restored ? "memtest: aborted; memory window remains mapped"
                              : "memtest: aborted; error: memory window is unavailable" );
}
