/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_memory.h"

#include "bsp_memory_internal.h"
#include "bsp_memory_verdict.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#if FORGIX_QSPI_PSRAM
#include "hardware/psram.h"
#endif




/***************************************************************************************
**
** Compiler Define Directives
**
***************************************************************************************/


#ifndef FORGIX_QSPI_CS1_GPIO
#define FORGIX_QSPI_CS1_GPIO 0
#endif

/* QMI chip select 1 is mapped here, immediately above the 16 MB chip-select-0
   window. See the PSRAM region in the SDK linker script. This is the XIP
   address the chip-select-1 PSRAM appears at once it has been mapped. */
#define PSRAM_WINDOW_BASE ( (uint32_t) 0x11000000u )

/* The same window through the no-allocate alias. Every test access goes through
   here, never through the cached window above: a read that hits the XIP cache
   verifies the cache and not the DRAM, and a write that dirties a line leaves
   the unified cache -- shared with the boot flash on chip select 0 -- with
   writeback traffic at a time nothing controls. Never touching the cached
   window is what makes the question "did the DRAM keep this" instead of "did
   the cache". */
#define PSRAM_NOCACHE_BASE ( PSRAM_WINDOW_BASE + ( XIP_NOCACHE_NOALLOC_BASE - XIP_BASE ) )

/* AP Memory's known-good-die byte, at offset 5 of the Read-ID response: the
   value the datasheet says the fitted part reports. A mismatch here means an
   unexpected vendor answered, not that the memory itself is broken. */
#define EXPECTED_KGD ( (uint8_t) 0x5du )




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* Filled in by the psram_eid_to_size override below, which the SDK calls from
   runtime_init. Written before main runs, so plain statics are sufficient.
   Refreshed afterwards through BSP_MemoryPsramRecordIdentity, which is how the
   identity probes in bsp_memory_identity.c replace the boot capture with bytes
   read in the datasheet's legal window. */
static uint8_t _reportedKgd;
static uint8_t _reportedEid;

#if FORGIX_QSPI_PSRAM
/* Whether the window was ever brought up by forcing the datasheet parameters.
   Latched rather than derived from control flow, because a successful force
   also latches the SDK's own initialised flag -- every later check then takes
   the auto-detected branch and would report forced=false for a device that
   was never auto-detected at all. */
static bool _psramForced;
#endif




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bool _FlashReadsCoherently( const uint32_t flashBytes );
#if FORGIX_QSPI_PSRAM
static bool _PsramWindowVerified( const uint32_t sizeBytes );
static bool _PsramHoldsAPattern( const uint32_t sizeBytes );
#endif




/***************************************************************************************
**
** Interrupt Handler Overrides
**
***************************************************************************************/


#if FORGIX_QSPI_PSRAM
/* The SDK declares psram_eid_to_size weak so applications can support parts it
   does not know. Overriding it here is not about the mapping -- that is
   reproduced exactly -- but about the arguments: this is the one place the raw
   identity bytes exist, read from the device by the SDK's own detection during
   runtime_init, and otherwise discarded once the size has been derived.
   Capturing them here costs no additional bus transaction and cannot disturb the
   device, which is precisely what every attempt to re-read them later did. */
/// <summary>
///     Overrides the SDK's weak hook to capture the raw identity bytes in passing.
///     The mapping is reproduced exactly; the point is the arguments, which exist
///     nowhere else and are discarded once the SDK has derived a size.
/// </summary>
/// <returns>
///     The device size in bytes, or zero if the vendor byte was unrecognised.
/// </returns>
size_t psram_eid_to_size( const uint8_t kgd, const uint8_t eid )
{
    _reportedKgd = kgd;
    _reportedEid = eid;

    if ( kgd != EXPECTED_KGD )
    {
        return 0;
    }

    /* Density lives in the top three bits of the EID, and the mapping is the
       SDK's (pico-sdk 2.3.0 psram.c), kept identical so overriding the hook
       changes nothing but observability. That includes the tail: unknown
       density codes stay at the 1 MiB base rather than being promoted to
       2 MiB, because a guessed-large size makes the moving-inversion sweep
       run off the die and file the mistake as a memory fault at an address
       nothing owns. */
    uint32_t psramSize = 1024u * 1024u;
    const uint8_t sizeId = eid >> 5;
    if ( sizeId == 4u )
    {
        psramSize *= 16u;
    }
    else if ( eid == 0x26u || sizeId == 2u || sizeId == 3u )
    {
        psramSize *= 8u;
    }
    else if ( sizeId == 1u )
    {
        psramSize *= 4u;
    }
    else if ( sizeId == 0u )
    {
        psramSize *= 2u;
    }
    return (size_t) psramSize;
}
#endif




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Reports both QSPI memories together, because they share SCLK and SD0..SD3 --
///     a fault on one shows up as the other misbehaving, and separate reports
///     would hide that.
/// </summary>
/// <returns>
///     Sizes, pass/fail, and the raw identity bytes captured during detection.
/// </returns>
bsp_memory_report_t BSP_MemoryCheck( void )
{
    bsp_memory_report_t report = { 0 };

    report.psram_kgd = _reportedKgd;
    report.psram_eid = _reportedEid;
    report.flash_bytes = PICO_FLASH_SIZE_BYTES;
    report.flash_ok = _FlashReadsCoherently( report.flash_bytes );

#if FORGIX_QSPI_PSRAM
    report.psram_enabled = true;
    if ( psram_is_available() )
    {
        report.psram_bytes = (uint32_t) psram_get_size();
        report.psram_ok = _PsramHoldsAPattern( report.psram_bytes );
    }
    else if ( BSP_MemoryPsramForceFromDatasheet() )
    {
        /* Auto-detection only compares the identity byte. This device answers
           Read-ID selectively and correctly, it just does not report AP Memory's
           vendor, so ask whether it works as memory rather than whether it says
           the right name. */
        report.psram_bytes = (uint32_t) psram_get_size();
        report.psram_ok = _PsramHoldsAPattern( report.psram_bytes );
    }
    /* From the latch, not from which branch ran: forcing sticks the SDK's
       initialised flag, so only the first check ever takes the forcing branch
       and a branch-derived flag would flip to false on the second report. */
    report.psram_forced = _psramForced;
#endif

    return report;
}


/* The pattern is each word's own uncached-alias address, XORed with a constant
   so word zero is not the all-zeroes a dead bus also returns. Address-derived
   is the property that matters: a smaller die aliasing the window lands an
   early chunk's pattern where a later chunk's belongs, and the value itself
   says which address the data actually came from. */
/// <summary>
///     Runs one chunk of one moving-inversion sweep pass through the uncached
///     window: plain word loops, interrupts on, the QMI arbitrating against
///     chip-select-0 XIP in hardware. Write chunks report ok when the chunk
///     exists; verify chunks report the first mismatch and its address. A
///     chunk beyond the device, or any chunk with the window down, is refused.
/// </summary>
/// <returns>
///     Whether the chunk held, and the failing address when it did not.
/// </returns>
bsp_memory_sweep_result_t BSP_MemoryPsramSweepChunk( bsp_memory_sweep_op op, uint32_t chunk_index )
{
    bsp_memory_sweep_result_t result = { 0 };

#if FORGIX_QSPI_PSRAM
    /* Chunk state lives with the caller, which is exactly why it is not
       trusted here: a stale count, or a device reporting more than the 16 MB
       window maps, would send the loop below writing into an unbacked alias
       and reporting whatever it read back as a fault at a fabricated address.
       A size of zero -- the window never came up, or it flunked verification
       and its advertised size was zeroed -- refuses every chunk. */
    const uint32_t available_chunks =
        (uint32_t) psram_get_size() / (uint32_t) BSP_MEMORY_PSRAM_SWEEP_CHUNK_BYTES;
    if ( chunk_index >= available_chunks )
    {
        return result;
    }

    const uint32_t base =
        (uint32_t) PSRAM_NOCACHE_BASE + chunk_index * (uint32_t) BSP_MEMORY_PSRAM_SWEEP_CHUNK_BYTES;
    volatile uint32_t *const ptr_chunk = (volatile uint32_t *) base;
    const uint32_t words =
        (uint32_t) BSP_MEMORY_PSRAM_SWEEP_CHUNK_BYTES / (uint32_t) sizeof( uint32_t );

    result.ok = true;
    for ( uint32_t index = 0; index < words; ++index )
    {
        const uint32_t address = base + index * (uint32_t) sizeof( uint32_t );
        const uint32_t pattern = address ^ 0x5a5a5a5au;

        if ( op == BSP_MEMORY_SWEEP_WRITE )
        {
            ptr_chunk[ index ] = pattern;
        }
        else
        {
            const uint32_t expected = op == BSP_MEMORY_SWEEP_VERIFY_INVERT ? pattern : ~pattern;
            if ( ptr_chunk[ index ] != expected )
            {
                result.ok = false;
                result.fail_address = address;
                return result;
            }
            if ( op == BSP_MEMORY_SWEEP_VERIFY_INVERT )
            {
                ptr_chunk[ index ] = ~pattern;
            }
        }
    }
#else
    (void) op;
    (void) chunk_index;
#endif

    return result;
}


#if FORGIX_QSPI_PSRAM
/* Configures chip select 1 from the datasheet instead of from what the device
   claims to be, then brings it up.

   APS1604M-3SQR at 3.3 V: 2 MByte, 84 MHz for linear-512 burst which is the
   conservative ceiling, tCEM 3 us at 105 C bounding how long chip select may
   stay asserted, and a deselect gap with margin over the specified minimum.

   psram_reinitialize is documented as unsafe against concurrent XIP, so it runs
   with interrupts off -- handlers live in flash.

   The CS1 FLASH_DEVINFO invariant, kept on every exit path because the BSP
   reads recovery's success out of this metadata (psram_get_size converts the
   devinfo size):
   - success: GPIO = the board's CS1 pin, size = 2M, window mapped and proven
     by the uncached probe;
   - failure before any hardware effect (bad params, or a reinitialize
     precondition): the GPIO and size found on entry are restored verbatim --
     nothing changed, so the metadata claims nothing new;
   - failure after the window is mapped (probe flunked): size = NONE, GPIO
     left at the real board pin. Restoring the entry size here could
     re-advertise 2M from an earlier successful force; NONE makes
     psram_get_size report 0, which is the whole containment -- every BSP
     consumer derives its bounds from that size, so BSP_MemoryCheck reports 0
     bytes with psram_ok false and the sweep refuses every chunk. What NONE
     does NOT undo, because the SDK offers no way to: psram_is_available stays
     true (psram_initialized is sticky), the CS1 setup callback that
     psram_reinitialize registered stays installed and may reconfigure the
     physical QMI window during later flash operations, and the QMI window
     configuration itself persists. That is why no BSP code treats
     psram_is_available alone as proof of a usable window. A retry lands back
     in this function -- the identify and dump paths call it every time -- and
     it reinstalls the candidate metadata first, so the retry does not fail
     the reinitialize precondition; it simply runs bring-up and verification
     again. */
/// <summary>
///     Brings chip select 1 up from the datasheet rather than from what the device
///     claims to be, for a part that works but reports an unexpected vendor.
///     Runs with interrupts off because psram_reinitialize is unsafe against
///     concurrent XIP and handlers live in flash. The SDK call alone proves
///     nothing: pico-sdk 2.3.0's psram_reinitialize fails only on its own
///     preconditions and never touches the device, and psram_get_size just reads
///     back the devinfo size this function wrote -- so success is only claimed
///     after an uncached write/readback shows the window actually holds data,
///     and every failure exit leaves the metadata per the invariant above.
/// </summary>
/// <returns>
///     True if the device came up and a two-word uncached probe held.
/// </returns>
bool BSP_MemoryPsramForceFromDatasheet( void )
{
    const uint previousGpio = flash_devinfo_get_cs_gpio( 1 );
    const flash_devinfo_size_t previousSize = flash_devinfo_get_cs_size( 1 );

    flash_devinfo_set_cs_gpio( 1, FORGIX_QSPI_CS1_GPIO );
    flash_devinfo_set_cs_size( 1, FLASH_DEVINFO_SIZE_2M );

    if ( psram_configure_params( 84u * 1000u * 1000u, 3000u, 50u ) != PICO_OK )
    {
        flash_devinfo_set_cs_gpio( 1, previousGpio );
        flash_devinfo_set_cs_size( 1, previousSize );
        return false;
    }

    const uint32_t interrupts = save_and_disable_interrupts();
    /* int is the SDK's own return type for psram_reinitialize; kept as-is
       since that is the honest type at this boundary. */
    const int result = psram_reinitialize();
    restore_interrupts( interrupts );

    if ( result != PICO_OK )
    {
        /* Both failure paths inside reinitialize return before touching the
           hardware, so boot-flash XIP and the QMI are exactly as they were --
           and the metadata goes back to exactly what was found. */
        flash_devinfo_set_cs_gpio( 1, previousGpio );
        flash_devinfo_set_cs_size( 1, previousSize );
        return false;
    }

    /* The window is mapped from here on even if verification fails, which is
       what the forced latch records -- "brought up by forcing", not
       "verified". */
    _psramForced = true;
    if ( !_PsramWindowVerified( (uint32_t) psram_get_size() ) )
    {
        /* XIP is running (reinitialize's flash_start_xip already ran), but the
           window flunked its probe: advertise no size, per the invariant
           above. The SDK's availability flag and CS1 callback remain set --
           containment rests entirely on the zero size. */
        flash_devinfo_set_cs_size( 1, FLASH_DEVINFO_SIZE_NONE );
        return false;
    }
    return true;
}


/// <summary>
///     Takes the identity bytes an identity probe read in the datasheet's legal
///     window and makes them what every later report shows. The storage lives
///     here rather than with the probe because BSP_MemoryCheck reports it and
///     the SDK's boot-time detection is the other writer.
/// </summary>
void BSP_MemoryPsramRecordIdentity( uint8_t kgd, uint8_t eid )
{
    _reportedKgd = kgd;
    _reportedEid = eid;
}
#endif




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/* The boot flash is proven readable by the fact that this code is executing from
   it, so the useful check is that it still reads back coherently: a stack
   pointer in SRAM and a reset vector inside the flash window. Bus contention on
   the shared QSPI lines corrupts reads rather than stopping them, so a garbled
   vector table is exactly what a CS1 problem looks like from here -- which is
   why the read goes through the no-allocate alias: the first line of flash is
   essentially guaranteed cache-resident from boot, and a read served out of
   SRAM cache verifies the cache, not the QSPI bus this check exists to accuse.
   The values compared are unchanged; the reset vector's contents still name
   the cached window, only the fetch path moves. */
/// <summary>
///     Checks the vector table rather than whether flash reads at all, which is
///     already proven by this code executing, and reads it over the QSPI bus
///     through the no-allocate alias rather than out of the XIP cache. Bus
///     contention corrupts reads instead of stopping them, so a garbled vector
///     table is what a chip-select fault looks like from here.
/// </summary>
/// <returns>
///     True if the stack pointer and reset vector are both plausible.
/// </returns>
static bool _FlashReadsCoherently( const uint32_t flashBytes )
{
    const volatile uint32_t *ptr_vectors = (const volatile uint32_t *) XIP_NOCACHE_NOALLOC_BASE;
    const uint32_t stackPointer = ptr_vectors[ 0 ];
    const uint32_t resetVector = ptr_vectors[ 1 ];

    return stackPointer > SRAM_BASE && stackPointer <= SRAM_END && resetVector >= XIP_BASE &&
           resetVector < XIP_BASE + flashBytes;
}

#if FORGIX_QSPI_PSRAM
/* Deliberately separate from _PsramHoldsAPattern: that probe is destructive and
   feeds BSP_MemoryCheck's own verdict, while this one must save and put back
   what it touches -- restoration runs against a window a future caller may be
   trusting -- and reusing the sweep probe here would double-probe on the check
   path that calls both. */
/// <summary>
///     Proves a freshly re-entered window by writing the planned patterns to its
///     first and last words through the uncached alias, reading them back, and
///     restoring the words it displaced. The plan and the verdict come from
///     bsp_memory_verdict, so the decision logic is host-tested; only the bus
///     access lives here.
/// </summary>
/// <returns>
///     True when both probe words read back exactly what was written.
/// </returns>
static bool _PsramWindowVerified( const uint32_t sizeBytes )
{
    const bsp_memory_probe_plan_t plan = BSP_MemoryVerdictProbePlan( sizeBytes );
    if ( !plan.viable )
    {
        return false;
    }

    volatile uint32_t *const ptr_window = (volatile uint32_t *) PSRAM_NOCACHE_BASE;
    const uint32_t savedFirst = ptr_window[ plan.first_word_index ];
    const uint32_t savedLast = ptr_window[ plan.last_word_index ];

    ptr_window[ plan.first_word_index ] = plan.first_pattern;
    ptr_window[ plan.last_word_index ] = plan.last_pattern;
    const uint32_t observedFirst = ptr_window[ plan.first_word_index ];
    const uint32_t observedLast = ptr_window[ plan.last_word_index ];

    ptr_window[ plan.first_word_index ] = savedFirst;
    ptr_window[ plan.last_word_index ] = savedLast;

    return BSP_MemoryVerdictProbeHeld( &plan, observedFirst, observedLast );
}

/* Writes every pattern before reading any of them back. Checking each write
   immediately would pass against a bus that merely echoes the last value, and
   would not catch address aliasing from a device smaller than it reports. */
/// <summary>
///     Writes every pattern before reading any back. Checking each write
///     immediately would pass against a bus that merely echoes the last value,
///     and would miss address aliasing from a device smaller than it claims.
/// </summary>
/// <returns>
///     True if all three patterns survived at their addresses.
/// </returns>
static bool _PsramHoldsAPattern( const uint32_t sizeBytes )
{
    if ( sizeBytes < (uint32_t) sizeof( uint32_t ) )
    {
        return false;
    }

    volatile uint32_t *const ptr_window = (volatile uint32_t *) PSRAM_NOCACHE_BASE;
    const uint32_t words = sizeBytes / (uint32_t) sizeof( uint32_t );
    const uint32_t indices[] = { 0, words / 2u, words - 1u };
    const uint32_t patterns[] = { 0xa5a5a5a5u, 0x5a5a5a5au, 0xdeadbeefu };
    const uint32_t count = (uint32_t) ( sizeof indices / sizeof indices[ 0 ] );

    for ( uint32_t index = 0; index < count; ++index )
    {
        ptr_window[ indices[ index ] ] = patterns[ index ];
    }
    for ( uint32_t index = 0; index < count; ++index )
    {
        if ( ptr_window[ indices[ index ] ] != patterns[ index ] )
        {
            return false;
        }
    }
    return true;
}
#endif
