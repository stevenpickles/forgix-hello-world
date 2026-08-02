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
    /* Read-ID bytes 4 and 5 from chip select 1 at each direct-mode RX delay,
       0 through 3: four pairs. Direct mode resets RXDELAY to 0 while boot stage 2
       gave the flash 2, and flash_do_cmd_cs never sets it, so the sampling point
       is a prime suspect for a response that decodes as shifted. */
    uint8_t qspi_cs1_sweep[8];
} bsp_memory_report_t;

bsp_memory_report_t bsp_memory_check(void);

#endif
