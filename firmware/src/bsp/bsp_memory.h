#ifndef FORGIX_BSP_MEMORY_H
#define FORGIX_BSP_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

/* Both QSPI memories share SCLK and SD0..SD3, so a fault on one shows up as the
   other misbehaving. Reporting them together makes that visible at boot. */
typedef struct {
    uint32_t flash_bytes;  /* configured size of the boot flash on chip select 0 */
    bool flash_ok;         /* readable, and the reset vector is sane */
    uint32_t psram_bytes;  /* detected size of the DRAM on chip select 1, 0 if absent */
    bool psram_ok;         /* survived a write/read pattern across its range */
    /* The whole Read-ID response from chip select 1. All zeros means nothing
       answered. Otherwise a device is present, and the byte positions say what
       it is: an AP Memory PSRAM puts KGD 0x5D at byte 5, while a JEDEC flash
       puts manufacturer, type and capacity at bytes 1 to 3. Reporting only the
       decoded size cannot distinguish "absent" from "present but unrecognised",
       and those lead to opposite conclusions. */
    uint8_t qspi_cs1_id[8];
    /* The same probe run against chip select 0, where the answer is known: the
       stacked W25Q16JV must report EF 40 15 at bytes 1 to 3. It calibrates the
       instrument -- without a reference reading, a strange CS1 response cannot
       be told apart from a probe that does not work. */
    uint8_t qspi_cs0_id[8];
    /* CLKDIV read back from DIRECT_CSR inside the transfer. Proves the divisor
       actually took effect, rather than being reset by the ROM's flash
       reconnection as an earlier attempt was. */
    uint8_t qspi_probe_clkdiv;
    /* Set when the serial identification failed and a quad-width reset was
       needed to recover the device. Normally false: a device answering in SPI
       mode must not be sent quad opcodes, which it would decode as noise. */
    bool qspi_cs1_recovered;
    /* The same transfer with a meaningless opcode instead of Read-ID. If this
       matches qspi_cs1_id then nothing is answering and we are reading the
       floating bus, not a device in a wrong mode -- a distinction no amount of
       resetting or reclocking can make. */
    uint8_t qspi_cs1_null[8];
    /* Whether the chip-select-1 device was brought up by forcing the datasheet's
       parameters rather than by auto-detection. Detection only checks the
       identity byte, so a device that reports an unexpected vendor can still be
       perfectly good memory -- identity and function are separate questions. */
    bool psram_forced;
} bsp_memory_report_t;

bsp_memory_report_t bsp_memory_check(void);

#endif
