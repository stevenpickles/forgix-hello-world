/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_ibit.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "application_diagnostics.h"
#include "application_ibit_internal.h"
#include "application_ibit_steps_board.h"
#include "application_ibit_steps_fpga.h"
#include "bsp.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


enum
{
    DETAIL_CAPACITY = 112,
    /* The frequency counter is gated for a finite window, so its answer carries
       a little quantisation. One percent is far tighter than any real fault and
       far looser than the measurement noise. */
    CLOCK_TOLERANCE_DIVISOR = 100,
};


typedef application_ibit_outcome_t ( *ibit_run_fn )( char *detail, size_t capacity );


typedef struct
{
    const char *name;
    /* Whether the step is meaningless once the FPGA is unreachable. Carried here
       rather than asked inside each step, because a step can span many passes
       and asking per pass would put a bus transaction in the foreground loop
       once a millisecond to re-answer a question that cannot change mid-step. */
    bool needs_fpga;
    ibit_run_fn run;
} ibit_step_t;




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* Not static, and the one exception to this section's name: the step modules
   are the other half of this state machine and reach it through
   application_ibit_internal.h. Nothing outside the three sees it. */
ibit_state_t ibit;

static const char *const OUTCOME_TEXT[] = {
    "PENDING", "PASS", "FAIL", "TIMEOUT", "SKIP", "INFO",
};




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bool fpga_reachable( void );

static void print_result( uint32_t index, application_ibit_outcome_t outcome, const char *detail );

static void tally( application_ibit_outcome_t outcome );

static void begin_step( uint32_t index );

static void begin_run( uint32_t first_index, uint32_t last_index );

static void print_summary( void );

static bool advance( void );

static void restore( void );

static void sequence_start( void );

static bool sequence_poll( void );

static void soak_start( void );

static bool soak_poll( void );

static void single_start( void );

static bool single_poll( void );

/* Takes the address of the step runners the two step modules export, so this
   table is what binds the sequence together; the steps themselves know nothing
   of their order or of each other. It follows the prototypes above for the
   reason E10 gives -- the activity constants below it need theirs declared
   first -- rather than sitting under Private Variable Declarations with the
   rest of the module's data. */
static const ibit_step_t STEPS[] = {
    { "Chip identity", false, application_ibit_step_chip_identity },
    { "Board identity", false, application_ibit_step_board_identity },
    { "Clocks", false, application_ibit_step_clocks },
    { "Memory sizing", false, application_ibit_step_memory_sizing },
    { "OTP flash device info", false, application_ibit_step_otp_devinfo },
    { "Boot flash", false, application_ibit_step_boot_flash },
    { "QSPI PSRAM", false, application_ibit_step_psram },
    { "Die temperature", false, application_ibit_step_temperature },
    { "USB link", false, application_ibit_step_usb },
    { "Watchdog and boot reason", false, application_ibit_step_watchdog },
    /* Not marked: this is the step that decides whether the FPGA is reachable,
       so skipping it on the grounds that the FPGA is unreachable would remove
       the only report of the fault. */
    { "FPGA configuration", false, application_ibit_step_fpga_configuration },
    { "FPGA register bus", true, application_ibit_step_fpga_registers },
    { "RGB LED", true, application_ibit_step_led },
    { "Button SW1", true, application_ibit_step_button },
    { "FPGA 32MHz clock", true, application_ibit_step_fpga_clock },
};

/* Each of SEQUENCE, SOAK and SINGLE takes the address of its own start/poll
   pair and shares restore for stop, so -- for the same reason as STEPS above
   -- the three follow the prototypes rather than sitting under Private
   Variable Declarations. */
static const application_activity_t SEQUENCE = {
    .name = "IBIT",
    .start = sequence_start,
    .poll = sequence_poll,
    .stop = restore,
};

static const application_activity_t SOAK = {
    .name = "IBIT soak",
    .start = soak_start,
    .poll = soak_poll,
    .stop = restore,
};

static const application_activity_t SINGLE = {
    .name = "IBIT step",
    .start = single_start,
    .poll = single_poll,
    .stop = restore,
};




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     How many steps the sequence has, taken from the STEPS table itself so the
///     menu, the single-step selector and the "n of m" counter on every result
///     line cannot drift from what actually runs.
/// </summary>
/// <returns>
///     The step count; valid indices run from zero to one less than this.
/// </returns>
uint32_t application_ibit_step_count( void )
{
    return (uint32_t) ( sizeof STEPS / sizeof STEPS[ 0 ] );
}


/// <summary>
///     Publishes a step's label so the menu can offer the steps without keeping a
///     second copy of the list. The index is not range-checked -- callers are
///     expected to have taken it from application_ibit_step_count.
/// </summary>
/// <returns>
///     The name, pointing into the constant table, so it outlives any caller.
/// </returns>
const char *application_ibit_step_name( uint32_t index )
{
    return STEPS[ index ].name;
}


/// <summary>
///     Hands the sequence out without starting it; the UI owns the start/poll/stop
///     lifecycle from here. There is one instance and one set of counters behind
///     all three activities, so only one of them may be running at a time.
/// </summary>
/// <returns>
///     The activity for one pass over every step, a constant with static lifetime.
/// </returns>
const application_activity_t *application_ibit_sequence( void )
{
    return &SEQUENCE;
}


/// <summary>
///     Hands out the variant whose poll restarts the run instead of finishing it,
///     so it never reports completion and an abort through the UI is the only way
///     it ends.
/// </summary>
/// <returns>
///     The soak activity, sharing the same module state as the other two.
/// </returns>
const application_activity_t *application_ibit_soak( void )
{
    return &SOAK;
}


/// <summary>
///     Latches which step to run as a side effect of handing the activity out,
///     because the activity struct carries no argument of its own. The index is
///     read when start() runs, so a second call before starting replaces the first
///     choice rather than queueing behind it.
/// </summary>
/// <returns>
///     The single-step activity, shared with every other caller.
/// </returns>
const application_activity_t *application_ibit_single( uint32_t index )
{
    ibit.index = index;
    return &SINGLE;
}


/* The same facts the sequence checks, printed without judging them. Useful when
   a board is behaving and the question is what it actually is, rather than
   whether it is well. */
/// <summary>
///     Prints the whole report inside one foreground pass rather than as an
///     activity, so it cannot be aborted and the watchdog marker is reclaimed
///     before every line: the SDK's stdio flush is untimed, and the marker is what
///     names the individual write a stalled host froze on.
/// </summary>
void application_ibit_print_board_report( void )
{
    const bsp_mcu_info_t info = BSP_McuInfo();
    const bsp_clocks_report_t clocks = BSP_ClocksReport();
    const bsp_adc_temperature_t temperature = BSP_AdcTemperature();

    application_ibit_mark_write();
    BSP_ConsolePrintf( "\nchip     manufacturer=%03X part=%04X revision=%u %s x%u\n",
                       info.manufacturer, info.part, info.revision,
                       info.architecture == BSP_MCU_ARCHITECTURE_ARM ? "Arm" : "RISC-V",
                       info.core_count );
    application_ibit_mark_write();
    BSP_ConsolePrintf( "board    %02X%02X%02X%02X%02X%02X%02X%02X\n", info.unique_id[ 0 ],
                       info.unique_id[ 1 ], info.unique_id[ 2 ], info.unique_id[ 3 ],
                       info.unique_id[ 4 ], info.unique_id[ 5 ], info.unique_id[ 6 ],
                       info.unique_id[ 7 ] );
    application_ibit_mark_write();
    BSP_ConsolePrintf( "package  id=%08lX device=%08lX%08lX valid=%u\n",
                       (unsigned long) info.package_id, (unsigned long) info.device_id_high,
                       (unsigned long) info.device_id_low, info.chip_info_valid );
    application_ibit_mark_write();
    BSP_ConsolePrintf( "memory   flash=%luKiB sram=%luKiB otp_cs0=0x%X otp_cs1=0x%X\n",
                       (unsigned long) ( info.flash_bytes / 1024u ),
                       (unsigned long) ( info.sram_bytes / 1024u ), info.otp_cs0_size_code,
                       info.otp_cs1_size_code );
    application_ibit_mark_write();
    BSP_ConsolePrintf( "clocks   sys=%lu usb=%lu ref=%lu peri=%lu adc=%lu Hz configured\n",
                       (unsigned long) clocks.sys_hz, (unsigned long) clocks.usb_hz,
                       (unsigned long) clocks.ref_hz, (unsigned long) clocks.peri_hz,
                       (unsigned long) clocks.adc_hz );
    application_ibit_mark_write();
    BSP_ConsolePrintf( "measured sys=%lu usb=%lu Hz\n", (unsigned long) clocks.measured_sys_hz,
                       (unsigned long) clocks.measured_usb_hz );
    application_ibit_mark_write();
    BSP_ConsolePrintf( "die      %ld milli-degrees C, raw=%u\n", (long) temperature.milli_celsius,
                       temperature.raw );
}


/// <summary>
///     Re-points the retained watchdog marker at the console write about to
///     happen. Repeated before every line rather than once per function, because
///     the marker is the only witness to which write was in progress if the
///     untimed stdio flush never returns.
/// </summary>
void application_ibit_mark_write( void )
{
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE );
}


/// <summary>
///     Time in the current step, measured from begin_step and against the stamp
///     advance() cached at the top of this pass -- so every deadline a step tests
///     within one pass is judged from the same instant, however long the pass took.
/// </summary>
/// <returns>
///     Milliseconds since the current step began.
/// </returns>
uint32_t application_ibit_step_elapsed_ms( void )
{
    return ibit.current_time_ms - ibit.step_started_ms;
}


/// <summary>
///     Whether a measurement sits inside one percent of what was expected. The
///     band is a fraction of expected rather than of measured, so it stays the
///     same width whichever way the error runs, and the difference is taken in
///     whichever order keeps it unsigned.
/// </summary>
/// <returns>
///     True when measured is within expected / CLOCK_TOLERANCE_DIVISOR of expected.
/// </returns>
bool application_ibit_within_tolerance( uint32_t measured, uint32_t expected )
{
    const uint32_t allowed = expected / CLOCK_TOLERANCE_DIVISOR;
    const uint32_t difference = measured > expected ? measured - expected : expected - measured;
    return difference <= allowed;
}


/// <summary>
///     Narrows a boolean to the pass/fail pair and nothing else. A step with any
///     other answer to give -- SKIP, INFO, TIMEOUT, PENDING -- returns it directly,
///     so a call to this is itself the sign that the step is a plain yes-or-no.
/// </summary>
/// <returns>
///     APPLICATION_IBIT_PASS or APPLICATION_IBIT_FAIL.
/// </returns>
application_ibit_outcome_t application_ibit_verdict( bool ok )
{
    return ok ? APPLICATION_IBIT_PASS : APPLICATION_IBIT_FAIL;
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/* Asked of the FPGA itself, every time, rather than read from BSP_FpgaIsReady.
   That latch is written by bring-up and cleared by the once-a-second health
   check, so it can lag a fresh fault by up to a second -- and a built-in test
   is exactly the tool someone reaches for when they suspect the latch is
   wrong. Three extra pings across a sequence is a cheap price for a skip
   decision made on this run's evidence. */
/// <summary>
///     Requires both the configuration pin and a correct design ID before it will
///     call the FPGA reachable: the pin alone would pass a part that is configured
///     but whose register bus has stopped answering, which is the fault the
///     dependent steps most need standing down for.
/// </summary>
/// <returns>
///     True when this run's own evidence says the FPGA can still be talked to.
/// </returns>
static bool fpga_reachable( void )
{
    return BSP_FpgaCdone() && BSP_FpgaPing() == BSP_FPGA_DESIGN_ID;
}


/* Left-aligned to a fixed column rather than dot-leadered. Dots read better, but
   every way of drawing them needs a "name too long" branch that no step name can
   currently reach, and an unreachable branch is a hole in the coverage gate that
   would have to be argued away rather than tested. */
/// <summary>
///     Numbers the line one-based against the whole table even during a
///     single-step run, so a lone result line still says which of the fifteen
///     steps produced it.
/// </summary>
static void print_result( uint32_t index, application_ibit_outcome_t outcome, const char *detail )
{
    application_ibit_mark_write();
    BSP_ConsolePrintf( "[%2lu/%2lu] %-26s %-7s  %s\n", (unsigned long) ( index + 1u ),
                       (unsigned long) application_ibit_step_count(), STEPS[ index ].name,
                       OUTCOME_TEXT[ outcome ], detail );
}


/// <summary>
///     Counts one outcome into one of five buckets. PENDING never arrives here --
///     advance() returns before calling this -- and the final branch absorbs
///     anything that is not one of the four named outcomes, so a new one lands in
///     the INFO count rather than vanishing from the summary.
/// </summary>
static void tally( application_ibit_outcome_t outcome )
{
    if ( outcome == APPLICATION_IBIT_PASS )
    {
        ++ibit.pass;
    }
    else if ( outcome == APPLICATION_IBIT_FAIL )
    {
        ++ibit.fail;
    }
    else if ( outcome == APPLICATION_IBIT_TIMEOUT )
    {
        ++ibit.timeout;
    }
    else if ( outcome == APPLICATION_IBIT_SKIP )
    {
        ++ibit.skip;
    }
    else
    {
        ++ibit.info;
    }
}


/* The dependency is settled once, here, rather than inside the steps. A step can
   span many passes, and asking per pass would put an FPGA transaction in the
   foreground loop once a millisecond to re-answer a question that cannot change
   while the step is running. */
/// <summary>
///     Clears the per-step scratch -- phase, the LED save flag and the step clock
///     -- and settles the skip decision for the whole step. The clock comes from
///     the stamp already in state rather than a fresh read, so the step's elapsed
///     time starts from the same instant its first pass sees.
/// </summary>
static void begin_step( uint32_t index )
{
    ibit.index = index;
    ibit.phase = 0;
    ibit.led_saved = false;
    ibit.step_started_ms = ibit.current_time_ms;
    ibit.skipping = STEPS[ index ].needs_fpga && !fpga_reachable();
}


/// <summary>
///     Resets the tally, the memory sample and both clocks, then opens the first
///     step. It deliberately leaves the three soak counters alone, which is what
///     lets a soak start each new run through here without losing the tally it has
///     been accumulating across them.
/// </summary>
static void begin_run( uint32_t first_index, uint32_t last_index )
{
    ibit.current_time_ms = BSP_TimeNowMs();
    ibit.first_index = first_index;
    ibit.last_index = last_index;
    ibit.sequence_started_ms = ibit.current_time_ms;
    ibit.pass = 0;
    ibit.fail = 0;
    ibit.timeout = 0;
    ibit.skip = 0;
    ibit.info = 0;
    ibit.memory_sampled = false;
    begin_step( first_index );
}


/// <summary>
///     Prints all five counts even where they are zero, so successive iterations in
///     a soak log line up column for column, and times the run from the stamp the
///     last pass cached rather than reading the clock a second time.
/// </summary>
static void print_summary( void )
{
    const uint32_t elapsed_ms = ibit.current_time_ms - ibit.sequence_started_ms;

    application_ibit_mark_write();
    BSP_ConsolePrintf( "\nIBIT: %lu PASS  %lu FAIL  %lu TIMEOUT  %lu SKIP  %lu INFO  in %lu.%lus\n",
                       (unsigned long) ibit.pass, (unsigned long) ibit.fail,
                       (unsigned long) ibit.timeout, (unsigned long) ibit.skip,
                       (unsigned long) ibit.info, (unsigned long) ( elapsed_ms / 1000u ),
                       (unsigned long) ( ( elapsed_ms / 100u ) % 10u ) );
}


/* One step per pass at most, so the foreground loop keeps feeding the watchdog
   whatever any individual step is waiting for. */
/// <summary>
///     One pass of the state machine: samples the clock once for everything that
///     follows, and either runs the current step or, where it stands down behind an
///     unreachable FPGA, produces the SKIP without calling its run function at all.
///     A PENDING answer is neither tallied nor printed; the step simply gets
///     another pass.
/// </summary>
/// <returns>
///     True while there is more to do, false once the run's last step has reported.
/// </returns>
static bool advance( void )
{
    char detail[ DETAIL_CAPACITY ] = { 0 };

    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_IBIT );
    ibit.current_time_ms = BSP_TimeNowMs();

    application_ibit_outcome_t outcome = APPLICATION_IBIT_SKIP;
    if ( ibit.skipping )
    {
        snprintf( detail, sizeof detail, "FPGA not responding; this test sits behind it" );
    }
    else
    {
        outcome = STEPS[ ibit.index ].run( detail, sizeof detail );
    }
    if ( outcome == APPLICATION_IBIT_PENDING )
    {
        return true;
    }

    tally( outcome );
    print_result( ibit.index, outcome, detail );
    if ( ibit.index == ibit.last_index )
    {
        return false;
    }

    begin_step( ibit.index + 1u );
    return true;
}


/* Anything left mid-run is put back here, because an abort is exactly when the
   LED is most likely to be sitting on a test colour. */
/// <summary>
///     Serves as stop() for all three activities, and is guarded by the saved flag,
///     so calling it twice or on a run that never reached the LED step does
///     nothing.
/// </summary>
static void restore( void )
{
    if ( ibit.led_saved )
    {
        BSP_LedRestore( &ibit.led_before );
        ibit.led_saved = false;
    }
}


/// <summary>
///     Announces the run and arms every step in the table. begin_run resets
///     everything a previous run left behind, so restarting after an abort begins
///     from a clean tally rather than continuing the old one.
/// </summary>
static void sequence_start( void )
{
    application_ibit_mark_write();
    BSP_ConsolePrintf( "\nInitiated built-in test\n\n" );
    begin_run( 0, application_ibit_step_count() - 1u );
}


/// <summary>
///     Prints the summary on the same pass that finds the last step finished, and
///     only then, since returning false is what stops the UI calling back at all.
/// </summary>
/// <returns>
///     True while steps remain, false once the summary has been printed.
/// </returns>
static bool sequence_poll( void )
{
    if ( advance() )
    {
        return true;
    }
    print_summary();
    return false;
}


/// <summary>
///     Clears the three cross-run counters that begin_run deliberately leaves
///     alone, so a soak started a second time does not inherit the totals of the
///     first.
/// </summary>
static void soak_start( void )
{
    ibit.soak_iterations = 0;
    ibit.soak_failures = 0;
    ibit.soak_timeouts = 0;
    application_ibit_mark_write();
    BSP_ConsolePrintf( "\nIBIT soak; press any key to stop\n\n" );
    begin_run( 0, application_ibit_step_count() - 1u );
}


/// <summary>
///     Never reports completion: at the end of a run it prints, folds the result
///     into the cross-run counters and immediately begins the next, so only an
///     abort through the UI ends a soak. An iteration counts once however many
///     steps failed within it, not once per failing step.
/// </summary>
/// <returns>
///     True on every pass, because a soak has no end of its own to report.
/// </returns>
static bool soak_poll( void )
{
    if ( advance() )
    {
        return true;
    }

    print_summary();
    ++ibit.soak_iterations;

    /* Failures and timeouts are tallied apart, and only failures are the
       headline. A soak is unattended by definition, so the button times out on
       every iteration; folding that into the failure count would make the one
       number a burn-in exists to produce equal the run count forever. */
    ibit.soak_failures += ( ibit.fail > 0u ) ? 1u : 0u;
    ibit.soak_timeouts += ( ibit.timeout > 0u ) ? 1u : 0u;

    application_ibit_mark_write();
    BSP_ConsolePrintf( "soak: %lu run(s), %lu with a failure, %lu with a timeout\n\n",
                       (unsigned long) ibit.soak_iterations, (unsigned long) ibit.soak_failures,
                       (unsigned long) ibit.soak_timeouts );

    begin_run( 0, application_ibit_step_count() - 1u );
    return true;
}


/// <summary>
///     Opens a run whose first and last step are both the index
///     application_ibit_single latched, which is what makes advance() stop after
///     one result instead of walking on to the next step.
/// </summary>
static void single_start( void )
{
    application_ibit_mark_write();
    BSP_ConsolePrintf( "\n%s\n\n", STEPS[ ibit.index ].name );
    begin_run( ibit.index, ibit.index );
}


/// <summary>
///     Ends without a summary, unlike the sequence: for a single step the one
///     result line advance() already printed is the whole report, and a tally of
///     one would say nothing further.
/// </summary>
/// <returns>
///     True until that step reports, false immediately afterwards.
/// </returns>
static bool single_poll( void )
{
    return advance();
}
