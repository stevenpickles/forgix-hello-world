/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_memory.h"

#include "bsp_memory_verdict.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#if FORGIX_QSPI_PSRAM
#include "hardware/psram.h"
#include "hardware/structs/qmi.h"
#include "hardware/xip_cache.h"
#include "pico/bootrom.h"
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

/* The probe clock answers to two datasheet limits at once. Read-ID has no
   wait cycles, so it carries a 33 MHz ceiling -- over that the QMI samples
   before the data is valid and returns displaced bytes. And the 8-byte
   Read-ID holds chip select low for 64 clocks in one stretch, which must fit
   inside tCEM (3 us at 105 C): the DRAM cannot refresh while selected, so an
   overrun risks the array. Divisor 8 (18.75 MHz) satisfied the ceiling but
   stretched the transfer to 3.4 us; 6 gives 25 MHz and 2.56 us, inside both.
   The asserts pin the arithmetic to clk_sys so neither limit can be broken by
   a clock change that never looked at this file. */
#define CS1_PROBE_CLKDIV ( (uint32_t) 6u )

_Static_assert( SYS_CLK_HZ / CS1_PROBE_CLKDIV <= 33000000u,
                "Read-ID must stay at or under its 33 MHz no-wait-state ceiling" );
_Static_assert( ( 64ull * CS1_PROBE_CLKDIV * 1000000000ull ) / SYS_CLK_HZ < 3000ull,
                "the 64-clock Read-ID must hold chip select shorter than the 3 us tCEM" );

/* tRST is 50 ns; 1500 cycles at 150 MHz is 10 us, a 200x margin. Spent as an
   in-RAM cycle spin rather than a timer wait because it elapses inside the
   XIP-down window, where the flash-resident busy_wait_us_32 cannot run. */
#define CS1_TRST_WAIT_CYCLES ( (uint32_t) 1500u )

/* Confirmation rates for the identity investigation: 5 MHz and 1 MHz beside
   the production 25 MHz. The investigation's extended 128-clock Read-ID holds
   chip select for 5.1 us, 25.6 us and 128 us at the three rates -- all past
   tCEM, and deliberately so: the ID register is static logic with no refresh
   dependency, the array contents are expendable during an investigation (the
   identify path resets the device and the next sweep rewrites it), and an
   identity that is bit-identical across a 25x clock spread cannot be a
   marginal-sampling artefact. Only the production probe, whose short transfer
   the asserts above measure, stays bound to the datasheet limits. */
#define CS1_SLOW_PROBE_CLKDIV ( (uint32_t) 30u )
#define CS1_SLOWEST_PROBE_CLKDIV ( (uint32_t) 150u )




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


#if FORGIX_QSPI_PSRAM
/* One entry of a direct-mode sequence against either chip select. Serial
   entries pump the TX/RX FIFOs full duplex; quad entries send a lone opcode
   four bits wide with the response discarded, reaching a device whose command
   decoder is in QPI mode. Each entry gets its own chip-select assertion, and
   the optional delay elapses after deassertion -- still inside the shared
   XIP-down window, so a tRST spent there is one no ROM traffic can
   interrupt. */
typedef struct cs_operation_t_tag
{
    const uint8_t *ptr_transmit;
    uint8_t *ptr_receive;
    size_t count;
    bool quad;
    uint32_t delay_cycles_after;
} cs_operation_t;
#endif




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* Filled in by the psram_eid_to_size override below, which the SDK calls from
   runtime_init. Written before main runs, so plain statics are sufficient. */
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
static bool _ForcePsramFromDatasheet( void );
static bool _PsramWindowVerified( const uint32_t sizeBytes );
static bool _PsramHoldsAPattern( const uint32_t sizeBytes );
/* The attributes ride the prototype so the definition reads plainly. The
   function must run from RAM: it suspends chip-select-0 XIP to use the bus,
   and flash-resident code cannot execute while it is down. */
static void _CsOperationSequence( const cs_operation_t *ptr_operations, size_t operationCount,
                                  uint32_t clkdiv, uint32_t csAssertBits )
    __attribute__( ( noinline, section( ".time_critical._CsOperationSequence" ) ) );
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
    else if ( _ForcePsramFromDatasheet() )
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


/// <summary>
///     Reads the chip-select-1 identity in the one window the datasheet allows --
///     straight after a global reset -- then re-enters QPI so the memory keeps
///     working. Reset, read and re-entry live in one call so an abort can never
///     leave the device reset but not re-initialised. The fresh bytes replace the
///     boot capture, which is nonsense after a warm reboot: the device was still
///     in QPI from the previous session when the SDK's serial Read-ID ran.
/// </summary>
/// <returns>
///     The identity bytes and whether the QPI re-entry brought the window back.
/// </returns>
bsp_memory_psram_identity_t BSP_MemoryPsramIdentify( void )
{
    bsp_memory_psram_identity_t identity = { 0 };

#if FORGIX_QSPI_PSRAM
    /* Flush any dirty line the unified cache holds before the ROM tears XIP
       down, so nothing pending is lost to the flush at the end of the window
       by luck of timing rather than by intent. */
    xip_cache_clean_all();

    /* Chip select 1 needs a non-zero size for the ROM to issue its XIP exit
       sequence to it; restored afterwards so nothing else sees the change. */
    const flash_devinfo_size_t previous = flash_devinfo_get_cs_size( 1 );
    flash_devinfo_set_cs_size( 1, FLASH_DEVINFO_SIZE_8K );

    /* Quad-width reset first, to recover a device stuck in QPI -- serial
       opcodes do not exist for it -- then the serial pair for a device already
       in SPI mode. One of the two always applies, and the serial pair also
       cleans up after the quad opcodes a serial device would have decoded as
       noise. The whole sequence runs inside one XIP-down window: 66h/99h is an
       atomic pair, and re-invoking the ROM between transfers used to fire an
       XIP exit sequence at the device mid-pair, voiding the reset -- and
       putting foreign traffic between the reset and the Read-ID that is only
       legal straight after it. */
    const uint8_t reset_enable[ 1 ] = { 0x66u };
    const uint8_t reset[ 1 ] = { 0x99u };
    const uint8_t read_id[ 8 ] = { 0x9fu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu };
    uint8_t discard[ 1 ] = { 0 };
    uint8_t response[ 8 ] = { 0 };

    const cs_operation_t operations[] = {
        { reset_enable, NULL, 1u, true, 0u },
        { reset, NULL, 1u, true, CS1_TRST_WAIT_CYCLES },
        { reset_enable, discard, 1u, false, 0u },
        { reset, discard, 1u, false, CS1_TRST_WAIT_CYCLES },
        { read_id, response, sizeof read_id, false, 0u },
    };
    _CsOperationSequence( operations, sizeof operations / sizeof operations[ 0 ], CS1_PROBE_CLKDIV,
                          QMI_DIRECT_CSR_ASSERT_CS1N_BITS );

    identity.kgd = response[ 5 ];
    identity.eid = response[ 6 ];

    /* _CsOperationSequence leaves XIP in the ROM's plain command mode. One
       ordinary SDK call restores the faster boot2 configuration: on this flash
       build, flash_do_cmd_cs ends by executing the boot2 image it copied out
       of boot RAM on its first-ever call and cached. Its trailing hardware
       restore also re-drives chip select 1 -- once psram_reinitialize has ever
       run, the SDK holds psram_initialize_internal as a sticky CS1 setup
       function -- which is harmless here because _ForcePsramFromDatasheet
       rebuilds CS1 properly right after. */
    const uint8_t restore_tx[ 1 ] = { 0x9fu };
    uint8_t restore_rx[ 1 ] = { 0 };
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_do_cmd_cs( restore_tx, restore_rx, sizeof restore_tx, 0 );
    restore_interrupts( interrupts );

    flash_devinfo_set_cs_size( 1, previous );

    /* The reset tore the device out of QPI; bring it back the same way boot
       does. restored=false means no verified window is advertised -- either
       re-entry failed before mapping anything, or the mapped window flunked
       its probe and its size was zeroed. Nothing stores data there, so the
       failure costs the rest of the firmware nothing, but it must be reported
       rather than papered over -- and calling this again retries the whole
       bring-up. */
    identity.restored = _ForcePsramFromDatasheet();

    /* Later reports now show bytes read in the legal window rather than
       whatever runtime_init captured. */
    _reportedKgd = identity.kgd;
    _reportedEid = identity.eid;
#endif

    return identity;
}


/* The chip-select-0 read runs through the same engine at the same divisor as
   the first PSRAM probe, so the two transactions differ in nothing but which
   select fell: a correct flash ID is positive proof the controller's launch
   and sample edges read a known device faithfully at these exact settings.
   The reported bytes are left alone on purpose -- this call is a witness, not
   a detector, and diag should keep showing what the production path read. */
/// <summary>
///     Captures the full Read-ID response from the boot flash as a sampling
///     control and from the PSRAM at three clock rates, resetting the PSRAM
///     first each time exactly as the production probe does, then re-enters
///     QPI. Every extended read knowingly overstays tCEM; see the header note.
/// </summary>
/// <returns>
///     Every response byte, the rate of each probe, and whether the QPI
///     re-entry brought the memory window back.
/// </returns>
bsp_memory_identity_dump_t BSP_MemoryIdentityDump( void )
{
    bsp_memory_identity_dump_t dump = { 0 };

#if FORGIX_QSPI_PSRAM
    const uint8_t read_id[ BSP_MEMORY_IDENTITY_RESPONSE_BYTES ] = {
        0x9fu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
        0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
    };
    const uint8_t reset_enable[ 1 ] = { 0x66u };
    const uint8_t reset[ 1 ] = { 0x99u };
    uint8_t discard[ 1 ] = { 0 };

    xip_cache_clean_all();

    const cs_operation_t control[] = {
        { read_id, dump.flash_response, sizeof read_id, false, 0u },
    };
    _CsOperationSequence( control, sizeof control / sizeof control[ 0 ], CS1_PROBE_CLKDIV,
                          QMI_DIRECT_CSR_ASSERT_CS0N_BITS );

    /* Non-zero size so the ROM's exit sequence reaches chip select 1 in each
       window; restored afterwards so nothing else sees the change. */
    const flash_devinfo_size_t previous = flash_devinfo_get_cs_size( 1 );
    flash_devinfo_set_cs_size( 1, FLASH_DEVINFO_SIZE_8K );

    const uint32_t divisors[ BSP_MEMORY_IDENTITY_PROBE_RATES ] = {
        CS1_PROBE_CLKDIV,
        CS1_SLOW_PROBE_CLKDIV,
        CS1_SLOWEST_PROBE_CLKDIV,
    };
    for ( uint32_t rate = 0; rate < (uint32_t) BSP_MEMORY_IDENTITY_PROBE_RATES; ++rate )
    {
        /* The full reset precedes every read: Read-ID is only legal straight
           after one, at any clock. */
        const cs_operation_t operations[] = {
            { reset_enable, NULL, 1u, true, 0u },
            { reset, NULL, 1u, true, CS1_TRST_WAIT_CYCLES },
            { reset_enable, discard, 1u, false, 0u },
            { reset, discard, 1u, false, CS1_TRST_WAIT_CYCLES },
            { read_id, dump.psram_response[ rate ], sizeof read_id, false, 0u },
        };
        _CsOperationSequence( operations, sizeof operations / sizeof operations[ 0 ],
                              divisors[ rate ], QMI_DIRECT_CSR_ASSERT_CS1N_BITS );
        dump.probe_hz[ rate ] = SYS_CLK_HZ / divisors[ rate ];
    }

    /* Same restore pair as the identify path: one ordinary SDK call brings
       back the faster boot2 XIP configuration (the cached boot2 copyout, plus
       the same incidental CS1 re-drive described there), then QPI re-entry
       brings the memory window back. */
    const uint8_t restore_tx[ 1 ] = { 0x9fu };
    uint8_t restore_rx[ 1 ] = { 0 };
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_do_cmd_cs( restore_tx, restore_rx, sizeof restore_tx, 0 );
    restore_interrupts( interrupts );

    flash_devinfo_set_cs_size( 1, previous );

    dump.psram_probed = true;
    dump.restored = _ForcePsramFromDatasheet();
#else
    /* No PSRAM support means no direct-mode engine, but the flash control
       read still has value; the SDK's helper performs the identical transfer
       at the QMI's reset-default clocking. Interrupts off because handlers
       live in flash and the helper takes XIP down. */
    const uint8_t read_id[ BSP_MEMORY_IDENTITY_RESPONSE_BYTES ] = {
        0x9fu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
        0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
    };
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_do_cmd_cs( read_id, dump.flash_response, sizeof read_id, 0 );
    restore_interrupts( interrupts );
#endif

    return dump;
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
static bool _ForcePsramFromDatasheet( void )
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

/* The direct-mode sequence flash_do_cmd_cs performs, generalised to a list of
   transfers inside one XIP-down window and reimplemented so the bus clock can
   be set at the one moment that matters -- the SDK's helper cannot be told a
   clock, since connect_internal_flash reconfigures QMI after any divisor
   written earlier.

   One window for the whole list is the point, not an optimisation: the ROM's
   flash_exit_xip fires an XIP exit sequence at every chip select with a
   non-zero devinfo size, so invoking the ROM per transfer would land foreign
   traffic between a 66h and its 99h -- voiding the reset pair -- and between
   a reset and the Read-ID that is only legal straight after it.

   Quad entries reach a device sitting in QPI mode, which decodes commands
   four bits wide across SIO[3:0] and is deaf to every serial opcode: OE
   drives all four lines, NOPUSH discards the response nobody wants.

   Runs from RAM with interrupts off: the ROM calls take chip-select-0 XIP
   down to talk to the bus, and any handler living in flash would fault while
   it is down. Inter-operation delays are in-window cycle spins for the same
   reason. XIP is left in the ROM's plain command mode on return; the caller
   restores the faster boot2 configuration with one ordinary flash_do_cmd_cs.

   The QSPI pad state the ROM leaves behind is not saved and restored here on
   purpose: the caller's recovery path re-runs flash_do_cmd_cs (which restores
   both the pads and the fast chip-select-0 timing -- it executes the boot2
   image it copied out of boot RAM on its first-ever call and cached, and its
   trailing restore also re-drives chip select 1 through the SDK's sticky CS1
   setup function once psram_reinitialize has ever run) and then
   psram_reinitialize itself (which rebuilds the chip-select-1 QMI window), so
   a save/restore here would duplicate what the re-entry path rebuilds. */
/// <summary>
///     Runs a list of direct-mode transfers against the given chip select at
///     the given clock divisor inside a single XIP-down window, each with its
///     own chip-select assertion, serial ones full duplex and quad ones
///     response-discarded, with optional post-deselect delays spent inside
///     the window.
/// </summary>
static void _CsOperationSequence( const cs_operation_t *ptr_operations, size_t operationCount,
                                  uint32_t clkdiv, uint32_t csAssertBits )
{
    rom_connect_internal_flash_fn connect_internal_flash =
        (rom_connect_internal_flash_fn) rom_func_lookup_inline( ROM_FUNC_CONNECT_INTERNAL_FLASH );
    rom_flash_exit_xip_fn flash_exit_xip =
        (rom_flash_exit_xip_fn) rom_func_lookup_inline( ROM_FUNC_FLASH_EXIT_XIP );
    rom_flash_flush_cache_fn flash_flush_cache =
        (rom_flash_flush_cache_fn) rom_func_lookup_inline( ROM_FUNC_FLASH_FLUSH_CACHE );
    rom_flash_enter_cmd_xip_fn flash_enter_cmd_xip =
        (rom_flash_enter_cmd_xip_fn) rom_func_lookup_inline( ROM_FUNC_FLASH_ENTER_CMD_XIP );

    const uint32_t interrupts = save_and_disable_interrupts();
    connect_internal_flash();
    flash_exit_xip();

    /* Now that the ROM has stopped touching QMI, impose the divisor. */
    hw_write_masked( &qmi_hw->direct_csr, clkdiv << QMI_DIRECT_CSR_CLKDIV_LSB,
                     QMI_DIRECT_CSR_CLKDIV_BITS );
    hw_set_bits( &qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS );

    for ( size_t operation = 0; operation < operationCount; ++operation )
    {
        const cs_operation_t *ptr_op = &ptr_operations[ operation ];

        hw_set_bits( &qmi_hw->direct_csr, csAssertBits );
        if ( ptr_op->quad )
        {
            qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS | QMI_DIRECT_TX_NOPUSH_BITS |
                                ( QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB ) |
                                ptr_op->ptr_transmit[ 0 ];
        }
        else
        {
            const uint8_t *ptr_transmit = ptr_op->ptr_transmit;
            uint8_t *ptr_receive = ptr_op->ptr_receive;
            size_t to_send = ptr_op->count;
            size_t to_receive = ptr_op->count;
            while ( to_send > 0u || to_receive > 0u )
            {
                const uint32_t status = qmi_hw->direct_csr;
                if ( to_send > 0u && ( status & QMI_DIRECT_CSR_TXFULL_BITS ) == 0u )
                {
                    qmi_hw->direct_tx = *ptr_transmit++;
                    --to_send;
                }
                if ( to_receive > 0u && ( status & QMI_DIRECT_CSR_RXEMPTY_BITS ) == 0u )
                {
                    *ptr_receive++ = (uint8_t) qmi_hw->direct_rx;
                    --to_receive;
                }
            }
        }
        while ( ( qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS ) != 0u )
        {
            tight_loop_contents();
        }
        hw_clear_bits( &qmi_hw->direct_csr, csAssertBits );

        if ( ptr_op->delay_cycles_after != 0u )
        {
            busy_wait_at_least_cycles( ptr_op->delay_cycles_after );
        }
    }

    hw_clear_bits( &qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS );
    flash_flush_cache();
    flash_enter_cmd_xip();
    restore_interrupts( interrupts );
}
#endif
