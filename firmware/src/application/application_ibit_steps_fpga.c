/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_ibit_steps_fpga.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "application_ibit.h"
#include "application_ibit_internal.h"
#include "application_time.h"
#include "bsp.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


enum
{
    LED_PHASE_MS = 200,
    BUTTON_POLL_MS = 50,
    EXPECTED_FPGA_HZ = 32000000,
    /* Wide enough that the millisecond stamps cannot matter: half a second of
       ticks judged at one percent leaves +/-5 ms of room, and the stamps carry
       about +/-1 ms of quantisation each. A shorter window would need a finer
       clock than the application layer has. */
    FPGA_CLOCK_SAMPLE_MS = 500,
};




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/* Also the only proof the 32 MHz oscillator and its GPIO 19 gate are alive. A
   design with no clock does not answer a ping at all, so a correct design ID
   here has already cleared them both. */
/// <summary>
///     Requires CDONE high and an exact design ID; the status pin is printed but
///     not judged. This is the step every FPGA-dependent skip is decided against,
///     which is why it is deliberately not itself marked as needing the FPGA.
/// </summary>
/// <returns>
///     PASS when the FPGA is configured and answering as the expected design.
/// </returns>
application_ibit_outcome_t application_ibit_step_fpga_configuration( char *detail, size_t capacity )
{
    const bool cdone = BSP_FpgaCdone();
    const uint8_t id = BSP_FpgaPing();

    snprintf( detail, capacity, "cdone=%u id=%02X status_pin=%u (32MHz oscillator implied)", cdone,
              id, BSP_FpgaStatusPin() );
    return application_ibit_verdict( cdone && id == BSP_FPGA_DESIGN_ID );
}


/// <summary>
///     Exercises the register bus through the LED registers, which are the only
///     writable block reachable from here, so it saves and restores the colour
///     around the test. All four registers must read back; the status byte is
///     printed for the log and is no part of the verdict.
/// </summary>
/// <returns>
///     PASS only when every one of the four written values came back unchanged.
/// </returns>
application_ibit_outcome_t application_ibit_step_fpga_registers( char *detail, size_t capacity )
{
    const uint8_t status = BSP_FpgaReadStatus();
    const bsp_led_state_t before = BSP_LedGet();

    /* A walking pattern rather than a constant: 0x00 and 0xFF are what a bus
       stuck low or high returns, and either would pass a test that wrote them. */
    BSP_LedSet( 0x5au, 0xa5u, 0x3cu, 0xc3u );
    const bsp_led_state_t readback = BSP_LedGet();
    BSP_LedRestore( &before );

    const bool ok = readback.red == 0x5au && readback.green == 0xa5u && readback.blue == 0x3cu &&
                    readback.brightness == 0xc3u;
    snprintf( detail, capacity, "status=%02X wrote 5A,A5,3C,C3 read %02X,%02X,%02X,%02X", status,
              readback.red, readback.green, readback.blue, readback.brightness );
    return application_ibit_verdict( ok );
}


/* Drives each channel on its own so a user watching can see which one is dead,
   and reads every one back so an unattended run still produces a verdict. */
/// <summary>
///     One colour per LED_PHASE_MS across several passes, capturing whatever the
///     user had showing on the first pass and putting it back on both exits. A
///     channel that fails to read back ends the step there rather than walking the
///     colours that remain.
/// </summary>
/// <returns>
///     PENDING between phases, PASS once every colour has read back, or FAIL naming
///     the phase that mismatched.
/// </returns>
application_ibit_outcome_t application_ibit_step_led( char *detail, size_t capacity )
{
    static const uint8_t COLOURS[][ 3 ] = {
        { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 255 }, { 0, 0, 0 },
    };
    static const uint32_t COLOUR_COUNT = sizeof COLOURS / sizeof COLOURS[ 0 ];

    if ( !ibit.led_saved )
    {
        ibit.led_before = BSP_LedGet();
        ibit.led_saved = true;
        ibit.phase = 0;
    }

    const uint32_t due = application_ibit_step_elapsed_ms() / LED_PHASE_MS;
    if ( due < ibit.phase )
    {
        return APPLICATION_IBIT_PENDING;
    }
    if ( ibit.phase < COLOUR_COUNT )
    {
        const uint8_t *colour = COLOURS[ ibit.phase ];
        BSP_LedSet( colour[ 0 ], colour[ 1 ], colour[ 2 ], 128 );
        const bsp_led_state_t readback = BSP_LedGet();
        if ( readback.red != colour[ 0 ] || readback.green != colour[ 1 ] ||
             readback.blue != colour[ 2 ] )
        {
            snprintf( detail, capacity, "readback mismatch at step %lu: %u,%u,%u",
                      (unsigned long) ibit.phase, readback.red, readback.green, readback.blue );
            BSP_LedRestore( &ibit.led_before );
            return APPLICATION_IBIT_FAIL;
        }
        ++ibit.phase;
        return APPLICATION_IBIT_PENDING;
    }

    BSP_LedRestore( &ibit.led_before );
    snprintf( detail, capacity,
              "red, green, blue and white all read back; previous colour restored" );
    return APPLICATION_IBIT_PASS;
}


/* Both halves have to move. The count alone could be a stuck event line and the
   level alone could be a pin held low, so requiring the pair is what separates a
   real press from a fault that looks like one. */
/// <summary>
///     Waits up to APPLICATION_IBIT_BUTTON_TIMEOUT_MS, sampling every
///     BUTTON_POLL_MS, and ends in TIMEOUT rather than FAIL when nobody presses --
///     an unattended run must not be a failing one. The count moving is what passes
///     it; the level is watched and reported but never decides.
/// </summary>
/// <returns>
///     PENDING while waiting, PASS on a press, TIMEOUT once the deadline passes.
/// </returns>
application_ibit_outcome_t application_ibit_step_button( char *detail, size_t capacity )
{
    if ( ibit.phase == 0 )
    {
        /* Cleared before the baseline is taken. The FPGA's counter is eight bits
           and saturates rather than wrapping, so on a board anyone has been
           pressing since power-up it eventually sits at 255 and never changes
           again -- and a test waiting for it to change could never pass on
           exactly the boards that have seen the most use. */
        BSP_ButtonClearCount();
        const bsp_button_state_t start = BSP_ButtonGetState();
        ibit.button_count_before = start.count;
        ibit.button_level_before = start.level;
        ibit.button_level_moved = false;
        ibit.phase = 1;
        ibit.next_poll_ms = ibit.current_time_ms;
        application_ibit_mark_write();
        BSP_ConsolePrintf( "        press SW1 within %lus ...\n",
                           (unsigned long) ( APPLICATION_IBIT_BUTTON_TIMEOUT_MS / 1000u ) );
        return APPLICATION_IBIT_PENDING;
    }
    if ( !application_deadline_reached( ibit.current_time_ms, ibit.next_poll_ms ) )
    {
        return APPLICATION_IBIT_PENDING;
    }

    ibit.next_poll_ms = ibit.current_time_ms + BUTTON_POLL_MS;
    const bsp_button_state_t now = BSP_ButtonGetState();
    ibit.button_level_moved = ibit.button_level_moved || ( now.level != ibit.button_level_before );

    /* The count alone decides it. The FPGA debounces and counts edges
       continuously; the level is a 50 ms sample of a line a person holds down
       for maybe a tenth of a second, so a brisk tap increments the count and is
       back at rest before the level is next read. Requiring both threw those
       presses away and reported a working button as a timeout.

       The level is still watched, and still reported, because it is the thing
       that says whether the pin moves as well as whether the counter does -- but
       reporting is all it can honestly support at this sample rate. A counter
       running free without any press shows up in how far it moved, which is why
       the count is printed rather than merely tested. */
    if ( now.count != ibit.button_count_before )
    {
        snprintf( detail, capacity, "pressed after %lu.%lus, count %u -> %u, level %s",
                  (unsigned long) ( application_ibit_step_elapsed_ms() / 1000u ),
                  (unsigned long) ( ( application_ibit_step_elapsed_ms() / 100u ) % 10u ),
                  ibit.button_count_before, now.count,
                  ibit.button_level_moved ? "seen to move" : "never sampled moving" );
        return APPLICATION_IBIT_PASS;
    }
    if ( application_ibit_step_elapsed_ms() < APPLICATION_IBIT_BUTTON_TIMEOUT_MS )
    {
        return APPLICATION_IBIT_PENDING;
    }

    snprintf( detail, capacity, "no press within %lus; count stayed at %u",
              (unsigned long) ( APPLICATION_IBIT_BUTTON_TIMEOUT_MS / 1000u ),
              ibit.button_count_before );
    return APPLICATION_IBIT_TIMEOUT;
}


/* This measures the ratio of the FPGA's oscillator to the MCU's clock, not a
   frequency against an absolute reference, so it cannot say which side is wrong
   on its own -- the MCU's clocks have their own step. What only this step can
   see is the counter falling short: an oscillator that stalled and recovered
   inside the window answers every ping and still fails here, which is what
   makes this worth running on soak. */
/// <summary>
///     Latches the FPGA's free-running counter twice, FPGA_CLOCK_SAMPLE_MS
///     apart, and requires the tick delta to match 32 MHz within one percent.
///     The delta is unsigned 32-bit subtraction, so a counter wrap mid-window --
///     every 134 seconds -- still measures correctly.
/// </summary>
/// <returns>
///     PENDING until the window closes, then PASS or FAIL.
/// </returns>
application_ibit_outcome_t application_ibit_step_fpga_clock( char *detail, size_t capacity )
{
    if ( ibit.phase == 0 )
    {
        ibit.fpga_tick_before = BSP_FpgaTickSample();
        ibit.fpga_tick_t0_ms = ibit.current_time_ms;
        ibit.phase = 1;
        return APPLICATION_IBIT_PENDING;
    }
    if ( !application_deadline_reached( ibit.current_time_ms,
                                        ibit.fpga_tick_t0_ms + FPGA_CLOCK_SAMPLE_MS ) )
    {
        return APPLICATION_IBIT_PENDING;
    }

    const uint32_t elapsed_ms = ibit.current_time_ms - ibit.fpga_tick_t0_ms;
    const uint32_t ticks = BSP_FpgaTickSample() - ibit.fpga_tick_before;
    /* Both products outgrow 32 bits before their divides -- 32e6 ticks/s over
       500 ms is 1.6e10, and ticks * 1000 peaks near 4.3e12 -- so each is taken
       in 64 and only the quotient comes back down. elapsed_ms is at least
       FPGA_CLOCK_SAMPLE_MS here, so neither divide can see zero. */
    const uint32_t expected = (uint32_t) ( ( (uint64_t) EXPECTED_FPGA_HZ * elapsed_ms ) / 1000u );
    const uint32_t measured_hz = (uint32_t) ( ( (uint64_t) ticks * 1000u ) / elapsed_ms );

    snprintf( detail, capacity, "fpga=%lu.%03luMHz over %lums, %lu ticks",
              (unsigned long) ( measured_hz / 1000000u ),
              (unsigned long) ( ( measured_hz / 1000u ) % 1000u ), (unsigned long) elapsed_ms,
              (unsigned long) ticks );
    return application_ibit_verdict( application_ibit_within_tolerance( ticks, expected ) );
}
