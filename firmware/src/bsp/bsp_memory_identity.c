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
** Private Function Declarations
**
***************************************************************************************/


#if FORGIX_QSPI_PSRAM
static void _RestoreBoot2Xip( void );
/* The attributes ride the prototype so the definition reads plainly. The
   function must run from RAM: it suspends chip-select-0 XIP to use the bus,
   and flash-resident code cannot execute while it is down. */
static void _CsOperationSequence( const bsp_memory_cs_operation_t *ptr_operations,
                                  uint32_t operationCount, uint32_t clkdiv,
                                  uint32_t csAssertBits )
    __attribute__( ( noinline, section( ".time_critical._CsOperationSequence" ) ) );
#endif




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


#if FORGIX_QSPI_PSRAM
/// <summary>
///     Runs one CS1 operation list while owning the whole dangerous interval:
///     cache clean, XIP exit, transfers, command-XIP entry, CS1 metadata restore,
///     boot2 restore, then and only then interrupt re-enable.
/// </summary>
void BSP_MemoryCs1OperationSequence( const bsp_memory_cs_operation_t *ptr_operations,
                                     uint32_t operationCount, uint32_t clkdiv )
{
    xip_cache_clean_all();

    const flash_devinfo_size_t previous = flash_devinfo_get_cs_size( 1 );
    flash_devinfo_set_cs_size( 1, FLASH_DEVINFO_SIZE_8K );

    const uint32_t interrupts = save_and_disable_interrupts();
    _CsOperationSequence( ptr_operations, operationCount, clkdiv,
                          QMI_DIRECT_CSR_ASSERT_CS1N_BITS );

    /* Do this before boot2 restoration. Leaving 8K advertised makes the ROM
       send another XIP-exit sequence to CS1, which is foreign traffic after
       the POST's legal Read-ID window and was the old branch's restore hazard. */
    flash_devinfo_set_cs_size( 1, previous );
    _RestoreBoot2Xip();
    restore_interrupts( interrupts );
}


#endif


/// <summary>
///     Adapts the boot-only POST report to the compact identity view consumed by
///     IBIT. No command is sent: Read-ID is legal only in the POST's post-reset
///     window, so runtime diagnostics always use this cached observation.
/// </summary>
/// <returns>
///     The cached identity bytes and whether boot restored the mapped window.
/// </returns>
bsp_memory_psram_identity_t BSP_MemoryPsramIdentify( void )
{
    const bsp_memory_post_report_t report = BSP_MemoryPsramPostReport();
    const bsp_memory_psram_identity_t identity = {
        .kgd = report.kgd,
        .eid = report.eid,
        .restored = report.restored,
    };
    return identity;
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
    flash_do_cmd_cs( restore_tx, restore_rx, sizeof restore_tx, 0 );
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
static void _CsOperationSequence( const bsp_memory_cs_operation_t *ptr_operations,
                                  uint32_t operationCount, uint32_t clkdiv,
                                  uint32_t csAssertBits )
{
    rom_connect_internal_flash_fn connect_internal_flash =
        (rom_connect_internal_flash_fn) rom_func_lookup_inline( ROM_FUNC_CONNECT_INTERNAL_FLASH );
    rom_flash_exit_xip_fn flash_exit_xip =
        (rom_flash_exit_xip_fn) rom_func_lookup_inline( ROM_FUNC_FLASH_EXIT_XIP );
    rom_flash_flush_cache_fn flash_flush_cache =
        (rom_flash_flush_cache_fn) rom_func_lookup_inline( ROM_FUNC_FLASH_FLUSH_CACHE );
    rom_flash_enter_cmd_xip_fn flash_enter_cmd_xip =
        (rom_flash_enter_cmd_xip_fn) rom_func_lookup_inline( ROM_FUNC_FLASH_ENTER_CMD_XIP );

    connect_internal_flash();
    flash_exit_xip();

    /* Now that the ROM has stopped touching QMI, impose the divisor. */
    hw_write_masked( &qmi_hw->direct_csr, clkdiv << QMI_DIRECT_CSR_CLKDIV_LSB,
                     QMI_DIRECT_CSR_CLKDIV_BITS );
    hw_set_bits( &qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS );

    for ( uint32_t operation = 0; operation < operationCount; ++operation )
    {
        const bsp_memory_cs_operation_t *ptr_op = &ptr_operations[ operation ];

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
            uint32_t to_send = ptr_op->count;
            uint32_t to_receive = ptr_op->count;
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
}
#endif
