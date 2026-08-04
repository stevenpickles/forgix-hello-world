/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_memory.h"

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#if FORGIX_QSPI_PSRAM
#include "hardware/psram.h"
#include "hardware/structs/qmi.h"
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




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* Filled in by the psram_eid_to_size override below, which the SDK calls from
   runtime_init. Written before main runs, so plain statics are sufficient. */
static uint8_t _reportedKgd;
static uint8_t _reportedEid;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bool _FlashReadsCoherently( const uint32_t flashBytes );
#if FORGIX_QSPI_PSRAM
static bool _ForcePsramFromDatasheet( void );
static bool _PsramHoldsAPattern( const uint32_t sizeBytes );
/* The attributes ride the prototypes so the definitions read plainly. Both
   functions must run from RAM: they suspend chip-select-0 XIP to use the bus,
   and flash-resident code cannot execute while it is down. */
static void _Cs1DirectTransfer( const uint8_t *ptr_transmit, uint8_t *ptr_receive, size_t count )
    __attribute__( ( noinline, section( ".time_critical._Cs1DirectTransfer" ) ) );
static void _Cs1QuadReset( void )
    __attribute__( ( noinline, section( ".time_critical._Cs1QuadReset" ) ) );
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
       SDK's, kept identical so overriding the hook changes nothing but
       observability. */
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
    else
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
        report.psram_forced = true;
        report.psram_bytes = (uint32_t) psram_get_size();
        report.psram_ok = _PsramHoldsAPattern( report.psram_bytes );
    }
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
    /* Chip select 1 needs a non-zero size for the ROM to issue its XIP exit
       sequence to it; restored afterwards so nothing else sees the change. */
    const flash_devinfo_size_t previous = flash_devinfo_get_cs_size( 1 );
    flash_devinfo_set_cs_size( 1, FLASH_DEVINFO_SIZE_8K );

    /* Quad-width reset first, to recover a device stuck in QPI -- serial
       opcodes do not exist for it -- then the serial pair for a device already
       in SPI mode. One of the two always applies, and the serial pair also
       cleans up after the quad opcodes a serial device would have decoded as
       noise. Each transfer is its own chip-select assertion, which is what the
       sequence requires. */
    const uint8_t reset_enable[ 1 ] = { 0x66u };
    const uint8_t reset[ 1 ] = { 0x99u };
    const uint8_t read_id[ 8 ] = { 0x9fu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu };
    uint8_t discard[ 8 ] = { 0 };
    uint8_t response[ 8 ] = { 0 };

    _Cs1QuadReset();
    busy_wait_us_32( 500 );
    _Cs1DirectTransfer( reset_enable, discard, sizeof reset_enable );
    _Cs1DirectTransfer( reset, discard, sizeof reset );
    /* tRST is 50 ns; this is generous and costs nothing. */
    busy_wait_us_32( 500 );
    _Cs1DirectTransfer( read_id, response, sizeof read_id );

    identity.kgd = response[ 5 ];
    identity.eid = response[ 6 ];

    /* _Cs1DirectTransfer leaves XIP in the ROM's plain command mode. One
       ordinary SDK call restores the faster boot2 configuration, since it does
       that copyout internally. */
    const uint8_t restore_tx[ 1 ] = { 0x9fu };
    uint8_t restore_rx[ 1 ] = { 0 };
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_do_cmd_cs( restore_tx, restore_rx, sizeof restore_tx, 0 );
    restore_interrupts( interrupts );

    flash_devinfo_set_cs_size( 1, previous );

    /* The reset tore the device out of QPI; bring it back the same way boot
       does. restored=false leaves the window down, which is inert -- nothing
       stores data there -- but must be reported rather than papered over. */
    identity.restored = _ForcePsramFromDatasheet();

    /* Later reports now show bytes read in the legal window rather than
       whatever runtime_init captured. */
    _reportedKgd = identity.kgd;
    _reportedEid = identity.eid;
#endif

    return identity;
}


/* The pattern is each word's own uncached-alias address, XORed with a constant
   so word zero is not the all-zeroes a dead bus also returns. Address-derived
   is the property that matters: a smaller die aliasing the window lands an
   early chunk's pattern where a later chunk's belongs, and the value itself
   says which address the data actually came from. */
/// <summary>
///     Runs one chunk of one moving-inversion sweep pass through the uncached
///     window: plain word loops, interrupts on, the QMI arbitrating against
///     chip-select-0 XIP in hardware. Write chunks always report ok; verify
///     chunks report the first mismatch and its address.
/// </summary>
/// <returns>
///     Whether the chunk held, and the failing address when it did not.
/// </returns>
bsp_memory_sweep_result_t BSP_MemoryPsramSweepChunk( bsp_memory_sweep_op op, uint32_t chunk_index )
{
    bsp_memory_sweep_result_t result = { 0 };

#if FORGIX_QSPI_PSRAM
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
   vector table is exactly what a CS1 problem looks like from here. */
/// <summary>
///     Checks the vector table rather than whether flash reads at all, which is
///     already proven by this code executing. Bus contention corrupts reads
///     instead of stopping them, so a garbled vector table is what a chip-select
///     fault looks like from here.
/// </summary>
/// <returns>
///     True if the stack pointer and reset vector are both plausible.
/// </returns>
static bool _FlashReadsCoherently( const uint32_t flashBytes )
{
    const uint32_t *ptr_vectors = (const uint32_t *) XIP_BASE;
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
   with interrupts off -- handlers live in flash. */
/// <summary>
///     Brings chip select 1 up from the datasheet rather than from what the device
///     claims to be, for a part that works but reports an unexpected vendor.
///     Runs with interrupts off because psram_reinitialize is unsafe against
///     concurrent XIP and handlers live in flash.
/// </summary>
/// <returns>
///     True if the device came up and reports a non-zero size.
/// </returns>
static bool _ForcePsramFromDatasheet( void )
{
    flash_devinfo_set_cs_gpio( 1, FORGIX_QSPI_CS1_GPIO );
    flash_devinfo_set_cs_size( 1, FLASH_DEVINFO_SIZE_2M );

    if ( psram_configure_params( 84u * 1000u * 1000u, 3000u, 50u ) != PICO_OK )
    {
        return false;
    }

    const uint32_t interrupts = save_and_disable_interrupts();
    /* int is the SDK's own return type for psram_reinitialize; kept as-is
       since that is the honest type at this boundary. */
    const int result = psram_reinitialize();
    restore_interrupts( interrupts );

    return result == PICO_OK && psram_get_size() > 0u;
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

/* The direct-mode sequence flash_do_cmd_cs performs, reimplemented so the bus
   clock can be set at the one moment that matters. Read ID takes zero wait
   cycles and is specified at 33 MHz maximum; over that it does not fail, it
   samples before the data is valid and returns displaced bytes. The SDK's
   helper cannot be told a clock -- connect_internal_flash reconfigures QMI
   after any divisor written earlier -- so the divisor is imposed here, after
   the ROM has finished.

   Runs from RAM with interrupts off: the ROM calls take chip-select-0 XIP down
   to talk to the bus, and any handler living in flash would fault while it is
   down. XIP is left in the ROM's plain command mode on return; the caller
   restores the faster boot2 configuration with one ordinary flash_do_cmd_cs. */
/// <summary>
///     One chip-select-1 direct-mode transfer at the probe clock, full duplex,
///     with chip-select-0 XIP suspended for the duration.
/// </summary>
static void _Cs1DirectTransfer( const uint8_t *ptr_transmit, uint8_t *ptr_receive, size_t count )
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
    hw_write_masked( &qmi_hw->direct_csr, CS1_PROBE_CLKDIV << QMI_DIRECT_CSR_CLKDIV_LSB,
                     QMI_DIRECT_CSR_CLKDIV_BITS );
    hw_set_bits( &qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS );

    hw_set_bits( &qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS );
    size_t to_send = count;
    size_t to_receive = count;
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
    while ( ( qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS ) != 0u )
    {
        tight_loop_contents();
    }
    hw_clear_bits( &qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS );
    hw_clear_bits( &qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS );

    flash_flush_cache();
    flash_enter_cmd_xip();
    restore_interrupts( interrupts );
}

/* A device sitting in QPI mode decodes commands four bits wide across SIO[3:0],
   so every serial opcode -- including the resets -- is never a valid command to
   it. Reset Enable and Reset exist in both widths, so issuing them quad reaches
   a device in either mode. OE drives all four lines, NOPUSH discards the
   response nobody wants. Each opcode gets its own chip-select assertion. */
/// <summary>
///     Quad-width reset-enable/reset pair on chip select 1, for a device that
///     may be in QPI mode and deaf to serial opcodes.
/// </summary>
static void _Cs1QuadReset( void )
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

    hw_write_masked( &qmi_hw->direct_csr, CS1_PROBE_CLKDIV << QMI_DIRECT_CSR_CLKDIV_LSB,
                     QMI_DIRECT_CSR_CLKDIV_BITS );
    hw_set_bits( &qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS );

    const uint8_t opcodes[ 2 ] = { 0x66u, 0x99u };
    for ( uint32_t index = 0; index < 2u; ++index )
    {
        hw_set_bits( &qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS );
        qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS | QMI_DIRECT_TX_NOPUSH_BITS |
                            ( QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB ) |
                            opcodes[ index ];
        while ( ( qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS ) != 0u )
        {
            tight_loop_contents();
        }
        hw_clear_bits( &qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS );
    }

    hw_clear_bits( &qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS );
    flash_flush_cache();
    flash_enter_cmd_xip();
    restore_interrupts( interrupts );
}
#endif
