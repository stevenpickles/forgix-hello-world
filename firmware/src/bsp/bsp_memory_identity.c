/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_memory.h"

#include "bsp_memory_internal.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#if FORGIX_QSPI_PSRAM
#include "hardware/structs/qmi.h"
#include "hardware/xip_cache.h"
#include "pico/bootrom.h"
#endif




/***************************************************************************************
**
** Compiler Define Directives
**
***************************************************************************************/


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
** Private Function Declarations
**
***************************************************************************************/


#if FORGIX_QSPI_PSRAM
static void _RestoreBoot2Xip( void );
/* The attributes ride the prototype so the definition reads plainly. The
   function must run from RAM: it suspends chip-select-0 XIP to use the bus,
   and flash-resident code cannot execute while it is down. */
static void _CsOperationSequence( const cs_operation_t *ptr_operations, size_t operationCount,
                                  uint32_t clkdiv, uint32_t csAssertBits )
    __attribute__( ( noinline, section( ".time_critical._CsOperationSequence" ) ) );
#endif




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


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

    _RestoreBoot2Xip();

    flash_devinfo_set_cs_size( 1, previous );

    /* The reset tore the device out of QPI; bring it back the same way boot
       does. restored=false means no verified window is advertised -- either
       re-entry failed before mapping anything, or the mapped window flunked
       its probe and its size was zeroed. Nothing stores data there, so the
       failure costs the rest of the firmware nothing, but it must be reported
       rather than papered over -- and calling this again retries the whole
       bring-up. */
    identity.restored = BSP_MemoryPsramForceFromDatasheet();

    /* Later reports now show bytes read in the legal window rather than
       whatever runtime_init captured. */
    BSP_MemoryPsramRecordIdentity( identity.kgd, identity.eid );
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

    _RestoreBoot2Xip();

    flash_devinfo_set_cs_size( 1, previous );

    dump.psram_probed = true;
    /* Same closing move as the identify path: QPI re-entry brings the memory
       window back, and its verification decides what restored may claim. */
    dump.restored = BSP_MemoryPsramForceFromDatasheet();
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




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


#if FORGIX_QSPI_PSRAM
/* One ordinary SDK call is enough because of what it drags behind it: on this
   flash build, flash_do_cmd_cs ends by executing the boot2 image it copied out
   of boot RAM on its first-ever call and cached. Its trailing hardware restore
   also re-drives chip select 1 -- once psram_reinitialize has ever run, the SDK
   holds psram_initialize_internal as a sticky CS1 setup function -- which is
   harmless to both callers here, because each rebuilds CS1 properly through
   BSP_MemoryPsramForceFromDatasheet right after. Interrupts stay off across the
   call for the usual reason: the helper takes XIP down and handlers live in
   flash. */
/// <summary>
///     Puts the faster boot2 XIP configuration back after _CsOperationSequence
///     has left XIP in the ROM's plain command mode. The Read-ID it issues is a
///     pretext -- the response goes nowhere, and the transfer exists only for
///     the restore its epilogue performs.
/// </summary>
static void _RestoreBoot2Xip( void )
{
    const uint8_t restore_tx[ 1 ] = { 0x9fu };
    uint8_t restore_rx[ 1 ] = { 0 };
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_do_cmd_cs( restore_tx, restore_rx, sizeof restore_tx, 0 );
    restore_interrupts( interrupts );
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
