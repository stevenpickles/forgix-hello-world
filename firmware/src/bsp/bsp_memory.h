#ifndef FORGIX_BSP_MEMORY_H
#define FORGIX_BSP_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

/* Both QSPI memories share SCLK and SD0..SD3, so a fault on one shows up as the
   other misbehaving. Reporting them together makes that visible at boot. */
typedef struct
{
    uint32_t flash_bytes;  /* configured size of the boot flash on chip select 0 */
    bool flash_ok;         /* readable, and the reset vector is sane */
    uint32_t psram_bytes;  /* detected size of the DRAM on chip select 1, 0 if absent */
    bool psram_ok;         /* survived a write/read pattern across its range */
    /* Whether the chip-select-1 device was brought up by forcing the datasheet's
       parameters rather than by auto-detection. Detection only checks the
       identity byte, so a device that reports an unexpected vendor can still be
       perfectly good memory -- identity and function are separate questions. */
    bool psram_forced;
    /* Vendor known-good-die and device-ID bytes as the chip reported them over
       QSPI. Captured during the SDK's own detection, which is the only moment
       they are readable: afterwards the device is switched to QPI so XIP can run
       four bits wide, and a serial Read-ID against it returns nonsense. Both
       zero means detection never ran. */
    uint8_t psram_kgd;
    uint8_t psram_eid;
} bsp_memory_report_t;

bsp_memory_report_t BSP_MemoryCheck(void);

#endif
