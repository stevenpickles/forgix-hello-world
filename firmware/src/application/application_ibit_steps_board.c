/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_ibit_steps_board.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "application_diagnostics.h"
#include "application_ibit.h"
#include "application_ibit_internal.h"
#include "bsp.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


enum
{
    /* Start-of-frame advances every millisecond while the host is framing, so
       twenty of them is already decisive without being a visible pause. */
    USB_SAMPLE_MS = 20,
    TEMPERATURE_MIN_MILLI_C = -20000,
    TEMPERATURE_MAX_MILLI_C = 85000,
    EXPECTED_SYS_HZ = 150000000,
    EXPECTED_USB_HZ = 48000000,
    EXPECTED_FLASH_BYTES = 2 * 1024 * 1024,
    EXPECTED_SRAM_BYTES = 520 * 1024,
};




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bsp_memory_report_t memory_report( void );

static const char *psram_schematic_note( void );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Passes only on Raspberry Pi's manufacturer code together with the RP2350
///     part number. Revision, architecture and core count are printed but not
///     judged -- an Arm image and a RISC-V image of the same part are both right.
/// </summary>
/// <returns>
///     PASS when the part identifies itself as an RP2350, FAIL otherwise.
/// </returns>
application_ibit_outcome_t application_ibit_step_chip_identity( char *detail, size_t capacity )
{
    const bsp_mcu_info_t info = BSP_McuInfo();
    const bool ok =
        info.manufacturer == BSP_MCU_MANUFACTURER_RASPBERRY_PI && info.part == BSP_MCU_PART_RP2350;

    snprintf( detail, capacity, "manufacturer=%03X part=%04X revision=%u %s x%u", info.manufacturer,
              info.part, info.revision,
              info.architecture == BSP_MCU_ARCHITECTURE_ARM ? "Arm" : "RISC-V", info.core_count );
    return application_ibit_verdict( ok );
}


/* All-zero and all-ones are what a bus that answered with nothing looks like, so
   both are rejected even though either is a legal-looking number. */
/// <summary>
///     Checks that a unique ID exists rather than that it is any particular value.
///     Nothing on the board says what the ID ought to be, so "the bus answered
///     with something" is the whole of what this can prove.
/// </summary>
/// <returns>
///     PASS unless every byte came back identical at 0x00 or at 0xFF.
/// </returns>
application_ibit_outcome_t application_ibit_step_board_identity( char *detail, size_t capacity )
{
    const bsp_mcu_info_t info = BSP_McuInfo();
    uint32_t zeroes = 0;
    uint32_t ones = 0;

    for ( uint32_t index = 0; index < BSP_MCU_UNIQUE_ID_BYTES; ++index )
    {
        zeroes += info.unique_id[ index ] == 0x00u;
        ones += info.unique_id[ index ] == 0xffu;
    }

    snprintf( detail, capacity, "%02X%02X%02X%02X%02X%02X%02X%02X", info.unique_id[ 0 ],
              info.unique_id[ 1 ], info.unique_id[ 2 ], info.unique_id[ 3 ], info.unique_id[ 4 ],
              info.unique_id[ 5 ], info.unique_id[ 6 ], info.unique_id[ 7 ] );
    return application_ibit_verdict( zeroes != BSP_MCU_UNIQUE_ID_BYTES &&
                                     ones != BSP_MCU_UNIQUE_ID_BYTES );
}


/* Measured, not configured. clock_get_hz reports what the SDK asked for, so a
   PLL that never locked still reads correct there and only shows up here. */
/// <summary>
///     Fails as soon as either measured clock leaves its one-percent band; both
///     must be in tolerance. The sys/usb ratio is reported and not tested, and a
///     dead USB clock prints a zero ratio rather than being divided by.
/// </summary>
/// <returns>
///     PASS when both measured frequencies are in band, FAIL if either is not.
/// </returns>
application_ibit_outcome_t application_ibit_step_clocks( char *detail, size_t capacity )
{
    const bsp_clocks_report_t clocks = BSP_ClocksReport();
    const bool sys_ok =
        application_ibit_within_tolerance( clocks.measured_sys_hz, EXPECTED_SYS_HZ );
    const bool usb_ok =
        application_ibit_within_tolerance( clocks.measured_usb_hz, EXPECTED_USB_HZ );
    /* RP2350-E12 wants clk_sys at least ten percent above clk_usb, and there is
       deliberately no separate verdict for it, because there cannot be a failing
       one. A measured clk_sys within one percent of 150 MHz is by construction
       more than ten percent above a measured clk_usb within one percent of
       48 MHz, so a test for the margin would be a branch nothing can take. It
       was previously read off the configured values, where it could fail --
       which only meant it was answering a different question: whether the SDK
       intended a legal ratio, which it always did.

       The ratio is printed instead so the margin stays visible. If the expected
       frequencies ever stop being pinned to 150 and 48, this has to go back to
       being a real check. */
    const uint32_t ratio_hundredths =
        clocks.measured_usb_hz == 0u
            ? 0u
            : (uint32_t) ( ( (uint64_t) clocks.measured_sys_hz * 100u ) / clocks.measured_usb_hz );

    snprintf( detail, capacity, "sys=%lu.%03luMHz usb=%lu.%03luMHz ref=%luHz sys/usb=%lu.%02lux",
              (unsigned long) ( clocks.measured_sys_hz / 1000000u ),
              (unsigned long) ( ( clocks.measured_sys_hz / 1000u ) % 1000u ),
              (unsigned long) ( clocks.measured_usb_hz / 1000000u ),
              (unsigned long) ( ( clocks.measured_usb_hz / 1000u ) % 1000u ),
              (unsigned long) clocks.ref_hz, (unsigned long) ( ratio_hundredths / 100u ),
              (unsigned long) ( ratio_hundredths % 100u ) );
    return application_ibit_verdict( sys_ok && usb_ok );
}


/// <summary>
///     Compares flash and SRAM against exact constants rather than a band, because
///     both are fixed properties of the part: any other figure means this image is
///     running on hardware it was not linked for.
/// </summary>
/// <returns>
///     PASS only on an exact match with both expected sizes.
/// </returns>
application_ibit_outcome_t application_ibit_step_memory_sizing( char *detail, size_t capacity )
{
    const bsp_mcu_info_t info = BSP_McuInfo();

    snprintf( detail, capacity, "flash=%luKiB sram=%luKiB",
              (unsigned long) ( info.flash_bytes / 1024u ),
              (unsigned long) ( info.sram_bytes / 1024u ) );
    return application_ibit_verdict( info.flash_bytes == EXPECTED_FLASH_BYTES &&
                                     info.sram_bytes == EXPECTED_SRAM_BYTES );
}


/* Reported, not judged. Measured on hardware this part answers 0x9 for chip
   select 0, which is the correct 2 MByte, and 0x0 for chip select 1 even though
   a 2 MByte device is fitted and working there. So one of the two is right and
   the other is not, and there is no way to tell them apart from the numbers
   alone -- which is exactly why this prints them and stops. Nothing in the
   firmware sizes a memory from here; flash comes from what the image was linked
   for and the DRAM from the SDK's own detection. */
/// <summary>
///     Always INFO, so it can never move the pass or fail counts. It is here for
///     what the two size codes tell a reader after the fact, not for anything this
///     run is entitled to decide from them.
/// </summary>
/// <returns>
///     APPLICATION_IBIT_INFO, unconditionally.
/// </returns>
application_ibit_outcome_t application_ibit_step_otp_devinfo( char *detail, size_t capacity )
{
    const bsp_mcu_info_t info = BSP_McuInfo();

    snprintf( detail, capacity, "cs0=0x%X cs1=0x%X (reported, not used to size anything)",
              info.otp_cs0_size_code, info.otp_cs1_size_code );
    return APPLICATION_IBIT_INFO;
}


/// <summary>
///     Rests entirely on the reset vector looking sane. The size is printed here
///     but judged by the memory sizing step, so the two are not spending a verdict
///     each on the same number.
/// </summary>
/// <returns>
///     PASS when the memory report says the reset vector is plausible.
/// </returns>
application_ibit_outcome_t application_ibit_step_boot_flash( char *detail, size_t capacity )
{
    const bsp_memory_report_t memory = memory_report();

    snprintf( detail, capacity, "%luKiB, reset vector %s",
              (unsigned long) ( memory.flash_bytes / 1024u ), memory.flash_ok ? "sane" : "BAD" );
    return application_ibit_verdict( memory.flash_ok );
}


/* The identity mismatch is reported and not failed. Identity and function are
   separate questions: this device sweeps clean across its whole range but calls
   itself KGD 0x0B EID 0x43 rather than AP Memory's 0x5D, so the fitted part is
   not the APS1604M-3SQR-SN on the schematic. Reading the package marking is
   what would settle it, and no test can.

   The identity is re-read every run in the one window the datasheet allows --
   straight after a global reset -- because the boot capture is only meaningful
   on a cold start. After a warm reboot the device was still in QPI when the
   SDK's serial Read-ID ran, and the bytes it kept are nonsense. */
/// <summary>
///     Reads the identity in its legal window, then drives a moving-inversion
///     sweep over the whole device one chunk per pass: the full range is written
///     before any of it is verified, which is what catches a smaller die
///     aliasing the window. The verdict is the sweep and nothing but the sweep;
///     the identity bytes are appended to the detail whatever they say.
/// </summary>
/// <returns>
///     SKIP when PSRAM is absent from this build, PENDING between chunks, then
///     PASS or FAIL on whether every chunk held.
/// </returns>
application_ibit_outcome_t application_ibit_step_psram( char *detail, size_t capacity )
{
    if ( ibit.phase == 0 )
    {
        const bsp_memory_report_t memory = memory_report();

        /* FORGIX_QSPI_PSRAM is a supported way to build this firmware, and with
           it off the device is never brought up. Reporting that as a failure
           would accuse a board of a fault that is really a build decision --
           and the two are indistinguishable from the numbers, since a device
           that was never enabled and one that failed detection both read zero
           bytes and not ok. */
        if ( !memory.psram_enabled )
        {
            snprintf( detail, capacity, "not enabled in this build (FORGIX_QSPI_PSRAM off)" );
            return APPLICATION_IBIT_SKIP;
        }
        if ( memory.psram_bytes < (uint32_t) BSP_MEMORY_PSRAM_SWEEP_CHUNK_BYTES )
        {
            snprintf( detail, capacity, "%luKiB reported; too small to sweep",
                      (unsigned long) ( memory.psram_bytes / 1024u ) );
            return APPLICATION_IBIT_FAIL;
        }

        /* Identity before sweep, because the read begins with a global reset
           that tears the device out of QPI; the same call re-enters it. A
           failed re-entry means there is no window to sweep. */
        ibit.psram_identity = BSP_MemoryPsramIdentify();
        if ( !ibit.psram_identity.restored )
        {
            snprintf( detail, capacity, "kgd=%02X eid=%02X read but QPI re-entry/verify failed",
                      ibit.psram_identity.kgd, ibit.psram_identity.eid );
            return APPLICATION_IBIT_FAIL;
        }
        ibit.psram_chunks = memory.psram_bytes / (uint32_t) BSP_MEMORY_PSRAM_SWEEP_CHUNK_BYTES;
        ibit.phase = 1;
        return APPLICATION_IBIT_PENDING;
    }

    const uint32_t swept_kib =
        ibit.psram_chunks * ( (uint32_t) BSP_MEMORY_PSRAM_SWEEP_CHUNK_BYTES / 1024u );
    const uint32_t chunk_ordinal = ibit.phase - 1u;
    const bsp_memory_sweep_op op = (bsp_memory_sweep_op) ( chunk_ordinal / ibit.psram_chunks );
    const bsp_memory_sweep_result_t result =
        BSP_MemoryPsramSweepChunk( op, chunk_ordinal % ibit.psram_chunks );

    if ( !result.ok )
    {
        snprintf( detail, capacity, "%luKiB sweep %lu/3 LOST at 0x%08lX, kgd=%02X eid=%02X%s",
                  (unsigned long) swept_kib, (unsigned long) ( (uint32_t) op + 1u ),
                  (unsigned long) result.fail_address, ibit.psram_identity.kgd,
                  ibit.psram_identity.eid, psram_schematic_note() );
        return APPLICATION_IBIT_FAIL;
    }
    if ( ibit.phase == 3u * ibit.psram_chunks )
    {
        snprintf( detail, capacity, "%luKiB sweep held, kgd=%02X eid=%02X%s",
                  (unsigned long) swept_kib, ibit.psram_identity.kgd, ibit.psram_identity.eid,
                  psram_schematic_note() );
        return APPLICATION_IBIT_PASS;
    }
    ++ibit.phase;
    return APPLICATION_IBIT_PENDING;
}


/* Banded, not compared. The uncalibrated sensor is several degrees out on a good
   day, so an exact figure would be a lie; a reading pinned at either rail is the
   fault worth catching, and that a band finds. */
/// <summary>
///     Accepts anything from -20 C to +85 C inclusive, which is wide on purpose:
///     the band exists to catch a sensor pinned at a rail, not to assess how warm
///     the board is. The raw converter code is printed so a suspicious reading can
///     still be traced.
/// </summary>
/// <returns>
///     PASS while the reading is inside the plausible band, FAIL at either rail.
/// </returns>
application_ibit_outcome_t application_ibit_step_temperature( char *detail, size_t capacity )
{
    const bsp_adc_temperature_t sample = BSP_AdcTemperature();
    const bool ok = sample.milli_celsius >= TEMPERATURE_MIN_MILLI_C &&
                    sample.milli_celsius <= TEMPERATURE_MAX_MILLI_C;

    /* The sign is carried separately rather than left to the integer division.
       Truncation toward zero loses it for anything between -1 C and 0 C, where
       -0.5 would have printed as "0.5C" -- a wrong reading rather than an
       imprecise one, and on the only part of the scale where the reader most
       needs to know which side of freezing the board is on. */
    const int32_t magnitude =
        sample.milli_celsius < 0 ? -sample.milli_celsius : sample.milli_celsius;

    snprintf( detail, capacity, "%s%ld.%01ldC raw=%u", sample.milli_celsius < 0 ? "-" : "",
              (long) ( magnitude / 1000 ), (long) ( ( magnitude / 100 ) % 10 ), sample.raw );
    return application_ibit_verdict( ok );
}


/* Two samples, because a single frame number proves nothing. The host's
   start-of-frame counter advancing is the only evidence available that the bus
   is live rather than merely enumerated. */
/// <summary>
///     Spans at least two passes: the first latches the frame number and yields,
///     and no verdict is given until USB_SAMPLE_MS has elapsed. All four
///     conditions must hold -- DTR asserted, not suspended, the frame counter
///     moved, and room left in the write buffer.
/// </summary>
/// <returns>
///     PENDING until the sample window closes, then PASS or FAIL.
/// </returns>
application_ibit_outcome_t application_ibit_step_usb( char *detail, size_t capacity )
{
    const bsp_usb_health_t health = BSP_UsbHealth();

    if ( ibit.phase == 0 )
    {
        ibit.usb_frame_before = health.frame_number;
        ibit.phase = 1;
        return APPLICATION_IBIT_PENDING;
    }
    if ( application_ibit_step_elapsed_ms() < USB_SAMPLE_MS )
    {
        return APPLICATION_IBIT_PENDING;
    }

    const bool framing = health.frame_number != ibit.usb_frame_before;
    snprintf( detail, capacity, "dtr=%u suspended=%u sof %s txfree=%lu", health.connected,
              health.suspended, framing ? "advancing" : "FROZEN",
              (unsigned long) health.write_available );
    return application_ibit_verdict( health.connected && !health.suspended && framing &&
                                     health.write_available > 0u );
}


/* A previous watchdog reset is a failure even though the board is plainly
   running now: it means something stopped feeding the loop, and the retained
   marker is the only witness to where.

   The reason comes from the diagnostics layer's boot-time snapshot rather than
   from a fresh BSP_WatchdogBootReason call. Arming the watchdog writes the
   scratch word watchdog_enable_caused_reboot consults, so asking again once the
   foreground loop is running reports a watchdog reset on every board -- which is
   how this step first failed on hardware that had powered up perfectly. */
/// <summary>
///     Two independent things fail this one step: a marker register that will not
///     round-trip a pattern, and a previous boot the watchdog forced. The marker is
///     put back to the built-in test's own value before returning, so a reset later
///     in the run still attributes itself here.
/// </summary>
/// <returns>
///     PASS only when the readback matched and the last boot was not a watchdog
///     reset.
/// </returns>
application_ibit_outcome_t application_ibit_step_watchdog( char *detail, size_t capacity )
{
    const bsp_boot_reason reason = application_diagnostics_boot_reason();
    static const char *const REASON_TEXT[] = { "power-on", "brownout", "watchdog", "other" };

    /* A pattern, not the marker the runner already wrote a few lines earlier. A
       register stuck at APPLICATION_DIAGNOSTICS_MARKER_IBIT would have passed a
       round trip that wrote the value it was already stuck at, which tests
       nothing. Restored immediately afterwards so a reset during the rest of
       this step still attributes itself to the built-in test. */
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_SELF_TEST_PATTERN );
    const bool marker_ok =
        BSP_WatchdogMarkerGet() == APPLICATION_DIAGNOSTICS_MARKER_SELF_TEST_PATTERN;
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_IBIT );

    snprintf( detail, capacity, "last boot %s, marker readback %s", REASON_TEXT[ reason ],
              marker_ok ? "ok" : "BAD" );
    return application_ibit_verdict( marker_ok && reason != BSP_BOOT_WATCHDOG );
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/* Samples both QSPI devices at most once per run. They share SCLK and SD0..SD3,
   so one report covers both and a second pass would only add bus traffic. */
/// <summary>
///     Returns the run's single memory sample, taking it on first use. begin_run
///     clears the flag, so boot flash and PSRAM always see identical numbers
///     within one run and the next run measures the devices again.
/// </summary>
/// <returns>
///     This run's report, freshly measured only on the first call after begin_run.
/// </returns>
static bsp_memory_report_t memory_report( void )
{
    if ( !ibit.memory_sampled )
    {
        ibit.memory = BSP_MemoryCheck();
        ibit.memory_sampled = true;
    }
    return ibit.memory;
}


/// <summary>
///     The schematic note for the PSRAM detail strings, keyed on this run's
///     legal-window bytes rather than the boot capture, which can be stale.
/// </summary>
/// <returns>
///     A suffix naming the identity mismatch, or an empty string on a match.
/// </returns>
static const char *psram_schematic_note( void )
{
    return ibit.psram_identity.kgd == 0x5du ? "" : " (not the part on the schematic)";
}
