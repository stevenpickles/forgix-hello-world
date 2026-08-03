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
    /* Whether the chip-select-1 device was brought up by forcing the datasheet's
       parameters rather than by auto-detection. Detection only checks the
       identity byte, so a device that reports an unexpected vendor can still be
       perfectly good memory -- identity and function are separate questions. */
    bool psram_forced;
} bsp_memory_report_t;

bsp_memory_report_t bsp_memory_check(void);

#endif
