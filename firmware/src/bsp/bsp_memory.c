#include "bsp_memory.h"

#include "hardware/flash.h"

#ifndef FORGIX_QSPI_CS1_GPIO
#define FORGIX_QSPI_CS1_GPIO 0
#endif

#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"

#include "pico/stdlib.h"

#if FORGIX_QSPI_PSRAM
#include "hardware/psram.h"
#endif

/* QMI chip select 1 is mapped here, immediately above the 16 MB chip-select-0
   window. See the PSRAM region in the SDK linker script. This is the XIP
   address the chip-select-1 PSRAM appears at once it has been mapped. */
#define PSRAM_WINDOW_BASE ((uint32_t)0x11000000u)

/* AP Memory's known-good-die byte, at offset 5 of the Read-ID response: the
   value the datasheet says the fitted part reports. A mismatch here means an
   unexpected vendor answered, not that the memory itself is broken. */
#define EXPECTED_KGD ((uint8_t)0x5du)

/* Filled in by the psram_eid_to_size override below, which the SDK calls from
   runtime_init. Written before main runs, so plain statics are sufficient. */
static uint8_t reported_kgd;
static uint8_t reported_eid;

/* The boot flash is proven readable by the fact that this code is executing from
   it, so the useful check is that it still reads back coherently: a stack
   pointer in SRAM and a reset vector inside the flash window. Bus contention on
   the shared QSPI lines corrupts reads rather than stopping them, so a garbled
   vector table is exactly what a CS1 problem looks like from here. */
static bool flash_reads_coherently(const uint32_t flash_bytes)
{
    const uint32_t *vectors = (const uint32_t *)XIP_BASE;
    const uint32_t stack_pointer = vectors[0];
    const uint32_t reset_vector = vectors[1];

    return stack_pointer > SRAM_BASE && stack_pointer <= SRAM_END &&
           reset_vector >= XIP_BASE && reset_vector < XIP_BASE + flash_bytes;
}

#if FORGIX_QSPI_PSRAM
/* The SDK declares psram_eid_to_size weak so applications can support parts it
   does not know. Overriding it here is not about the mapping -- that is
   reproduced exactly -- but about the arguments: this is the one place the raw
   identity bytes exist, read from the device by the SDK's own detection during
   runtime_init, and otherwise discarded once the size has been derived.
   Capturing them here costs no additional bus transaction and cannot disturb the
   device, which is precisely what every attempt to re-read them later did. */
size_t psram_eid_to_size(const uint8_t kgd, const uint8_t eid)
{
    reported_kgd = kgd;
    reported_eid = eid;

    if (kgd != EXPECTED_KGD)
    {
        return 0;
    }

    /* Density lives in the top three bits of the EID, and the mapping is the
       SDK's, kept identical so overriding the hook changes nothing but
       observability. */
    size_t psram_size = 1024u * 1024u;
    const uint8_t size_id = eid >> 5;
    if (size_id == 4u)
    {
        psram_size *= 16u;
    }
    else if (eid == 0x26u || size_id == 2u || size_id == 3u)
    {
        psram_size *= 8u;
    }
    else if (size_id == 1u)
    {
        psram_size *= 4u;
    }
    else
    {
        psram_size *= 2u;
    }
    return psram_size;
}

/* Configures chip select 1 from the datasheet instead of from what the device
   claims to be, then brings it up.

   APS1604M-3SQR at 3.3 V: 2 MByte, 84 MHz for linear-512 burst which is the
   conservative ceiling, tCEM 3 us at 105 C bounding how long chip select may
   stay asserted, and a deselect gap with margin over the specified minimum.

   psram_reinitialize is documented as unsafe against concurrent XIP, so it runs
   with interrupts off -- handlers live in flash. */
static bool force_psram_from_datasheet(void)
{
    flash_devinfo_set_cs_gpio(1, FORGIX_QSPI_CS1_GPIO);
    flash_devinfo_set_cs_size(1, FLASH_DEVINFO_SIZE_2M);

    if (psram_configure_params(84u * 1000u * 1000u, 3000u, 50u) != PICO_OK)
    {
        return false;
    }

    const uint32_t interrupts = save_and_disable_interrupts();
    const int result = psram_reinitialize();
    restore_interrupts(interrupts);

    return result == PICO_OK && psram_get_size() > 0u;
}

/* Writes every pattern before reading any of them back. Checking each write
   immediately would pass against a bus that merely echoes the last value, and
   would not catch address aliasing from a device smaller than it reports. */
static bool psram_holds_a_pattern(const uint32_t size_bytes)
{
    if (size_bytes < sizeof(uint32_t))
    {
        return false;
    }

    volatile uint32_t *const window = (volatile uint32_t *)PSRAM_WINDOW_BASE;
    const uint32_t words = size_bytes / sizeof(uint32_t);
    const uint32_t indices[] = {0, words / 2u, words - 1u};
    const uint32_t patterns[] = {0xa5a5a5a5u, 0x5a5a5a5au, 0xdeadbeefu};
    const uint32_t count = sizeof indices / sizeof indices[0];

    for (uint32_t index = 0; index < count; ++index)
    {
        window[indices[index]] = patterns[index];
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        if (window[indices[index]] != patterns[index])
        {
            return false;
        }
    }
    return true;
}
#endif

bsp_memory_report_t BSP_MemoryCheck(void)
{
    bsp_memory_report_t report = {0};

    report.psram_kgd = reported_kgd;
    report.psram_eid = reported_eid;
    report.flash_bytes = PICO_FLASH_SIZE_BYTES;
    report.flash_ok = flash_reads_coherently(report.flash_bytes);

#if FORGIX_QSPI_PSRAM
    if (psram_is_available())
    {
        report.psram_bytes = (uint32_t)psram_get_size();
        report.psram_ok = psram_holds_a_pattern(report.psram_bytes);
    }
    else if (force_psram_from_datasheet())
    {
        /* Auto-detection only compares the identity byte. This device answers
           Read-ID selectively and correctly, it just does not report AP Memory's
           vendor, so ask whether it works as memory rather than whether it says
           the right name. */
        report.psram_forced = true;
        report.psram_bytes = (uint32_t)psram_get_size();
        report.psram_ok = psram_holds_a_pattern(report.psram_bytes);
    }
#endif

    return report;
}
