#include "bsp_memory.h"

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/qmi.h"
#include "hardware/sync.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#if FORGIX_QSPI_PSRAM
#include "hardware/psram.h"
#endif

/* QMI chip select 1 is mapped here, immediately above the 16 MB chip-select-0
   window. See the PSRAM region in the SDK linker script. */
enum { PSRAM_WINDOW_BASE = 0x11000000u };

/* The boot flash is proven readable by the fact that this code is executing from
   it, so the useful check is that it still reads back coherently: a stack
   pointer in SRAM and a reset vector inside the flash window. Bus contention on
   the shared QSPI lines corrupts reads rather than stopping them, so a garbled
   vector table is exactly what a CS1 problem looks like from here. */
static bool flash_reads_coherently(uint32_t flash_bytes) {
    const uint32_t *vectors = (const uint32_t *)XIP_BASE;
    uint32_t stack_pointer = vectors[0];
    uint32_t reset_vector = vectors[1];

    return stack_pointer > SRAM_BASE && stack_pointer <= SRAM_END &&
           reset_vector >= XIP_BASE && reset_vector < XIP_BASE + flash_bytes;
}

#if FORGIX_QSPI_PSRAM
/* The same Read-ID sequence the SDK's own detection uses: command 0x9F followed
   by seven no-ops to clock the response out, with the KGD and EID landing in
   bytes 5 and 6. Chip select 1 needs a non-zero size for the ROM to issue its
   XIP exit sequence to it, so that is set for the duration and put back after.

   Runs from RAM with interrupts off: flash_do_cmd_cs turns XIP off to talk to
   the bus, and any handler living in flash would fault while it is down. */
static void __no_inline_not_in_flash_func(cs1_transfer)(const uint8_t *transmit, uint8_t *receive,
                                                        size_t count) {
    uint32_t interrupts = save_and_disable_interrupts();
    flash_do_cmd_cs(transmit, receive, count, 1);
    restore_interrupts(interrupts);
}

/* The device has to be reset before it can be identified, and the reason is the
   missing pull-up. With chip select held asserted from power-up, this part does
   not ignore the bootrom's flash traffic -- it decodes it as its own command
   stream. Arbitrary opcodes walk it into QPI mode or some other state, and that
   happens again on every boot, before any of our code runs. So an unreset device
   answers Read-ID with whatever mode it was left in, which is what the first
   probe showed: a response that was neither absent nor AP Memory's 0x0D / 0x5D.

   0xF5 leaves QPI mode if it is in it, then 0x66 / 0x99 is the standard
   reset-enable / reset pair, understood by the AP Memory parts and by the
   JEDEC-style devices that might occupy the same footprint. Each transfer is its
   own chip-select assertion, which is what the sequence requires. */
/* JEDEC Read-ID against the boot flash, as a control for the CS1 probe. No reset
   is sent here: this device is the one we are executing from. */
static void identify_qspi_cs0(uint8_t *response) {
    const uint8_t read_id[8] = {0x9fu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu};
    uint32_t interrupts = save_and_disable_interrupts();
    flash_do_cmd_cs(read_id, response, sizeof read_id, 0);
    restore_interrupts(interrupts);
}

/* The direct-mode sequence flash_do_cmd_cs performs, reimplemented so the bus
   clock can be set at the one moment that matters.

   Read ID takes zero wait cycles and is specified at 33 MHz maximum, unlike the
   133 MHz burst commands which have four to eight wait cycles covering the
   device's output-valid time. Over that limit the transfer does not fail, it
   samples before the data is valid and returns displaced bytes -- which is what
   00 00 00 00 66 0B 43 57 looks like.

   The SDK's helper cannot be told a clock. Setting DIRECT_CSR before calling it
   does nothing, because connect_internal_flash runs inside and reconfigures QMI
   afterwards; an earlier sweep of RXDELAY returned four identical results for
   exactly that reason. So the divisor is written here, after the ROM has
   finished, and read back so the caller can prove it took.

   XIP is left in the ROM's plain command mode on return. The caller restores the
   faster boot2 configuration by making one ordinary flash_do_cmd_cs call, which
   does that copyout internally. */
static void __no_inline_not_in_flash_func(cs1_direct_transfer)(const uint8_t *transmit,
                                                               uint8_t *receive, size_t count,
                                                               uint32_t clkdiv,
                                                               uint8_t *observed_clkdiv) {
    rom_connect_internal_flash_fn connect_internal_flash =
        (rom_connect_internal_flash_fn)rom_func_lookup_inline(ROM_FUNC_CONNECT_INTERNAL_FLASH);
    rom_flash_exit_xip_fn flash_exit_xip =
        (rom_flash_exit_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_EXIT_XIP);
    rom_flash_flush_cache_fn flash_flush_cache =
        (rom_flash_flush_cache_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_FLUSH_CACHE);
    rom_flash_enter_cmd_xip_fn flash_enter_cmd_xip =
        (rom_flash_enter_cmd_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_ENTER_CMD_XIP);

    connect_internal_flash();
    flash_exit_xip();

    /* Now that the ROM has stopped touching QMI, impose the divisor. */
    hw_write_masked(&qmi_hw->direct_csr, clkdiv << QMI_DIRECT_CSR_CLKDIV_LSB,
                    QMI_DIRECT_CSR_CLKDIV_BITS);
    hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS);
    *observed_clkdiv =
        (uint8_t)((qmi_hw->direct_csr & QMI_DIRECT_CSR_CLKDIV_BITS) >> QMI_DIRECT_CSR_CLKDIV_LSB);

    hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
    size_t to_send = count;
    size_t to_receive = count;
    while (to_send || to_receive) {
        uint32_t status = qmi_hw->direct_csr;
        if (to_send && !(status & QMI_DIRECT_CSR_TXFULL_BITS)) {
            qmi_hw->direct_tx = *transmit++;
            --to_send;
        }
        if (to_receive && !(status & QMI_DIRECT_CSR_RXEMPTY_BITS)) {
            *receive++ = (uint8_t)qmi_hw->direct_rx;
            --to_receive;
        }
    }
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
        tight_loop_contents();
    }
    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS);

    flash_flush_cache();
    flash_enter_cmd_xip();
}

/* A device sitting in QPI mode decodes commands four bits wide across SIO[3:0],
   so every serial opcode we have sent it -- including the resets -- was never a
   valid command. That is the shape of the evidence: a response that does not
   move across reboots, across resets, or across a four-fold change in bus clock
   is a width mismatch, not a timing problem.

   Reset Enable and Reset exist in both widths, so issuing them quad reaches a
   device in either mode. OE drives all four lines, NOPUSH discards the response
   we do not want. Each opcode gets its own chip-select assertion. */
static void __no_inline_not_in_flash_func(cs1_quad_reset)(uint32_t clkdiv) {
    rom_connect_internal_flash_fn connect_internal_flash =
        (rom_connect_internal_flash_fn)rom_func_lookup_inline(ROM_FUNC_CONNECT_INTERNAL_FLASH);
    rom_flash_exit_xip_fn flash_exit_xip =
        (rom_flash_exit_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_EXIT_XIP);
    rom_flash_flush_cache_fn flash_flush_cache =
        (rom_flash_flush_cache_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_FLUSH_CACHE);
    rom_flash_enter_cmd_xip_fn flash_enter_cmd_xip =
        (rom_flash_enter_cmd_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_ENTER_CMD_XIP);

    connect_internal_flash();
    flash_exit_xip();

    hw_write_masked(&qmi_hw->direct_csr, clkdiv << QMI_DIRECT_CSR_CLKDIV_LSB,
                    QMI_DIRECT_CSR_CLKDIV_BITS);
    hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS);

    static const uint8_t opcodes[2] = {0x66u, 0x99u};
    for (uint32_t index = 0; index < 2u; ++index) {
        hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
        qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS | QMI_DIRECT_TX_NOPUSH_BITS |
                            (QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB) |
                            opcodes[index];
        while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
            tight_loop_contents();
        }
        hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
    }

    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS);
    flash_flush_cache();
    flash_enter_cmd_xip();
}

/* Divisors of the 150 MHz system clock, straddling the 33 MHz Read-ID limit:
   4 is 37.5 MHz and over it, 6 is 25 MHz, 8 is 18.75 MHz, 16 is 9.4 MHz. If the
   over-limit entry is garbled and the others read 0D 5D, the clock was the
   fault. */
static const uint8_t probe_clkdivs[4] = {4u, 6u, 8u, 16u};

const uint8_t *bsp_memory_probe_clkdivs(void) {
    return probe_clkdivs;
}

/* Read ID may only be issued straight after a global reset plus tRST, so the
   reset pair precedes it at the same clock. Both are zero-wait commands.

   Note the reset is issued serially. If the device is sitting in QPI mode these
   opcodes need quad width to be understood, so a serial reset cannot reach it --
   that remains untested and is the next thing to try if the clock is not the
   whole story. */
/* Control: the identical transfer with an opcode the device cannot recognise.
   A real responder gives a different answer than it gives to Read-ID. */
static void probe_qspi_cs1_null(uint8_t *response, uint32_t clkdiv) {
    flash_devinfo_size_t previous = flash_devinfo_get_cs_size(1);
    flash_devinfo_set_cs_size(1, FLASH_DEVINFO_SIZE_8K);

    const uint8_t nonsense[8] = {0x00u, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu};
    uint8_t seen = 0;
    uint32_t interrupts = save_and_disable_interrupts();
    cs1_direct_transfer(nonsense, response, sizeof nonsense, clkdiv, &seen);
    restore_interrupts(interrupts);

    uint8_t restore_tx[1] = {0x9fu};
    uint8_t restore_rx[1] = {0};
    interrupts = save_and_disable_interrupts();
    flash_do_cmd_cs(restore_tx, restore_rx, sizeof restore_tx, 0);
    restore_interrupts(interrupts);

    flash_devinfo_set_cs_size(1, previous);
}

static void identify_qspi_cs1(uint8_t *response, uint32_t clkdiv, uint8_t *observed_clkdiv) {
    flash_devinfo_size_t previous = flash_devinfo_get_cs_size(1);
    /* Chip select 1 needs a non-zero size for the ROM to issue its XIP exit
       sequence to it; restored afterwards so nothing else sees the change. */
    flash_devinfo_set_cs_size(1, FLASH_DEVINFO_SIZE_8K);

    const uint8_t reset_enable[1] = {0x66u};
    const uint8_t reset[1] = {0x99u};
    const uint8_t read_id[8] = {0x9fu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu};
    uint8_t discard[8] = {0};
    uint8_t seen = 0;

    /* Quad first, to recover a device stuck in QPI; then serial, which is what a
       device already in SPI mode understands. One of the two always applies. */
    uint32_t interrupts = save_and_disable_interrupts();
    cs1_quad_reset(clkdiv);
    restore_interrupts(interrupts);
    busy_wait_us_32(500);

    interrupts = save_and_disable_interrupts();
    cs1_direct_transfer(reset_enable, discard, sizeof reset_enable, clkdiv, &seen);
    cs1_direct_transfer(reset, discard, sizeof reset, clkdiv, &seen);
    restore_interrupts(interrupts);

    busy_wait_us_32(500); /* tRST is 50 ns; this is generous and costs nothing */

    interrupts = save_and_disable_interrupts();
    cs1_direct_transfer(read_id, response, sizeof read_id, clkdiv, &seen);
    restore_interrupts(interrupts);

    *observed_clkdiv = seen;

    /* cs1_direct_transfer leaves XIP in the ROM's plain command mode. One
       ordinary SDK call restores the faster boot2 configuration, since it does
       that copyout internally. */
    uint8_t restore_tx[1] = {0x9fu};
    uint8_t restore_rx[1] = {0};
    interrupts = save_and_disable_interrupts();
    flash_do_cmd_cs(restore_tx, restore_rx, sizeof restore_tx, 0);
    restore_interrupts(interrupts);

    flash_devinfo_set_cs_size(1, previous);
}

/* Writes every pattern before reading any of them back. Checking each write
   immediately would pass against a bus that merely echoes the last value, and
   would not catch address aliasing from a device smaller than it reports. */
static bool psram_holds_a_pattern(uint32_t size_bytes) {
    if (size_bytes < sizeof(uint32_t)) {
        return false;
    }

    volatile uint32_t *window = (volatile uint32_t *)PSRAM_WINDOW_BASE;
    const uint32_t words = size_bytes / sizeof(uint32_t);
    const uint32_t indices[] = {0, words / 2u, words - 1u};
    const uint32_t patterns[] = {0xa5a5a5a5u, 0x5a5a5a5au, 0xdeadbeefu};
    const uint32_t count = sizeof indices / sizeof indices[0];

    for (uint32_t index = 0; index < count; ++index) {
        window[indices[index]] = patterns[index];
    }
    for (uint32_t index = 0; index < count; ++index) {
        if (window[indices[index]] != patterns[index]) {
            return false;
        }
    }
    return true;
}
#endif

bsp_memory_report_t bsp_memory_check(void) {
    bsp_memory_report_t report = {0};

    report.flash_bytes = PICO_FLASH_SIZE_BYTES;
    report.flash_ok = flash_reads_coherently(report.flash_bytes);

#if FORGIX_QSPI_PSRAM
    /* Probed once and cached. The identity cannot change while powered, and
       repeating it would put a QSPI transaction on chip select 1 every time
       anything asks for a report -- a soak pinging `diag` would then be
       exercising the shared bus on a schedule, which is a variable the run is
       supposed to be holding still. */
    static bool identified;
    static uint8_t cached_cs1[8];
    static uint8_t cached_cs0[8];
    static uint8_t cached_sweep[8];
    static uint8_t cached_clkdiv;
    static uint8_t cached_null[8];
    if (!identified) {
        identify_qspi_cs0(cached_cs0);
        probe_qspi_cs1_null(cached_null, 16u);
        for (uint32_t entry = 0; entry < 4u; ++entry) {
            uint8_t attempt[8] = {0};
            uint8_t seen = 0;
            identify_qspi_cs1(attempt, probe_clkdivs[entry], &seen);
            cached_sweep[entry * 2u] = attempt[4];
            cached_sweep[entry * 2u + 1u] = attempt[5];
            cached_clkdiv = seen;
            /* Keep the slowest attempt as the reported identity: furthest inside
               the Read-ID limit, so most likely to be the true one. */
            if (entry == 3u) {
                for (uint32_t index = 0; index < sizeof attempt; ++index) {
                    cached_cs1[index] = attempt[index];
                }
            }
        }
        identified = true;
    }
    for (uint32_t index = 0; index < sizeof cached_cs1; ++index) {
        report.qspi_cs1_id[index] = cached_cs1[index];
        report.qspi_cs0_id[index] = cached_cs0[index];
        report.qspi_cs1_null[index] = cached_null[index];
        report.qspi_cs1_sweep[index] = cached_sweep[index];
    }
    report.qspi_probe_clkdiv = cached_clkdiv;
    {
    }

    if (psram_is_available()) {
        report.psram_bytes = (uint32_t)psram_get_size();
        report.psram_ok = psram_holds_a_pattern(report.psram_bytes);
    }
#endif

    return report;
}
