#include "bsp_memory.h"

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
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
static void __no_inline_not_in_flash_func(read_qspi_cs1_id)(uint8_t *response) {
    uint8_t transmit[8] = {0x9fu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu};
    uint8_t receive[8] = {0};

    flash_devinfo_size_t previous = flash_devinfo_get_cs_size(1);
    flash_devinfo_set_cs_size(1, FLASH_DEVINFO_SIZE_8K);

    uint32_t interrupts = save_and_disable_interrupts();
    flash_do_cmd_cs(transmit, receive, sizeof transmit, 1);
    restore_interrupts(interrupts);

    flash_devinfo_set_cs_size(1, previous);
    for (uint32_t index = 0; index < sizeof receive; ++index) {
        response[index] = receive[index];
    }
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
    static uint8_t cached_id[8];
    if (!identified) {
        read_qspi_cs1_id(cached_id);
        identified = true;
    }
    for (uint32_t index = 0; index < sizeof cached_id; ++index) {
        report.qspi_cs1_id[index] = cached_id[index];
    }

    if (psram_is_available()) {
        report.psram_bytes = (uint32_t)psram_get_size();
        report.psram_ok = psram_holds_a_pattern(report.psram_bytes);
    }
#endif

    return report;
}
