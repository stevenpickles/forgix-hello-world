#include "bsp_memory.h"

#include "hardware/regs/addressmap.h"
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
    if (psram_is_available()) {
        report.psram_bytes = (uint32_t)psram_get_size();
        report.psram_ok = psram_holds_a_pattern(report.psram_bytes);
    }
#endif

    return report;
}
