#include "bsp_memory.h"

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/qmi.h"
#include "hardware/sync.h"
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

static void identify_qspi_cs1(uint8_t *response, uint32_t rx_delay) {
    /* Chip select 1 comes out of reset with the QMI's default timing while chip
       select 0 was given clkdiv 2 and rxdelay 2 by boot stage 2. Both devices sit
       on the same pins at the same clock, so matching them is right regardless.

       Note this governs memory-mapped XIP accesses only. The identification
       below goes through flash_do_cmd_cs, which uses direct mode and takes its
       sampling from DIRECT_CSR, so this does not affect the response recorded
       here -- setting it made no difference to the bytes read back. It is kept
       because it is the correct setting for any later XIP use of CS1, not
       because it fixed anything. */
    qmi_hw->m[1].timing = qmi_hw->m[0].timing;

    /* Direct mode resets RXDELAY to 0 while the flash runs at 2, and
       flash_do_cmd_cs only sets the enable bit -- it never configures sampling.
       WARNING: this write does not survive. flash_do_cmd_cs calls the ROM's
       connect_internal_flash, which resets QMI state, so every delay in the
       sweep produced identical bytes. The sweep is inconclusive, not negative;
       testing sampling properly needs the direct-mode sequence reimplemented
       here rather than borrowed from the SDK. */
    hw_write_masked(&qmi_hw->direct_csr, rx_delay << QMI_DIRECT_CSR_RXDELAY_LSB,
                    QMI_DIRECT_CSR_RXDELAY_BITS);

    flash_devinfo_size_t previous = flash_devinfo_get_cs_size(1);
    /* Chip select 1 needs a non-zero size for the ROM to issue its XIP exit
       sequence to it; restored afterwards so nothing else sees the change. */
    flash_devinfo_set_cs_size(1, FLASH_DEVINFO_SIZE_8K);

    const uint8_t exit_qpi[1] = {0xf5u};
    const uint8_t reset_enable[1] = {0x66u};
    const uint8_t reset[1] = {0x99u};
    uint8_t discard[8] = {0};

    cs1_transfer(exit_qpi, discard, sizeof exit_qpi);
    cs1_transfer(reset_enable, discard, sizeof reset_enable);
    cs1_transfer(reset, discard, sizeof reset);
    /* tRST: AP Memory specifies a few microseconds; XIP is back on between
       transfers, so an ordinary busy wait is safe here. */
    busy_wait_us_32(500);

    /* Read-ID, then no-ops to clock the response out. An AP Memory part puts its
       manufacturer at byte 4 and KGD 0x5D at byte 5. */
    const uint8_t read_id[8] = {0x9fu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu};
    cs1_transfer(read_id, response, sizeof read_id);

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
    if (!identified) {
        identify_qspi_cs0(cached_cs0);
        for (uint32_t delay = 0; delay < 4u; ++delay) {
            uint8_t attempt[8] = {0};
            identify_qspi_cs1(attempt, delay);
            cached_sweep[delay * 2u] = attempt[4];
            cached_sweep[delay * 2u + 1u] = attempt[5];
            if (delay == 2u) {
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
        report.qspi_cs1_sweep[index] = cached_sweep[index];
    }

    if (psram_is_available()) {
        report.psram_bytes = (uint32_t)psram_get_size();
        report.psram_ok = psram_holds_a_pattern(report.psram_bytes);
    }
#endif

    return report;
}
