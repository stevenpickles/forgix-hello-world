#ifndef FORGIX_BSP_MEMORY_H
#define FORGIX_BSP_MEMORY_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_types.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


/* Both QSPI memories share SCLK and SD0..SD3, so a fault on one shows up as the
   other misbehaving. Reporting them together makes that visible at boot. */
typedef struct bsp_memory_report_t_tag
{
    uint32_t flash_bytes; /* configured size of the boot flash on chip select 0 */
    bool flash_ok;        /* readable, and the reset vector is sane */
    uint32_t psram_bytes; /* detected size of the DRAM on chip select 1, 0 if absent */
    bool psram_ok;        /* survived a write/read pattern across its range */
    /* Whether this image was built to bring the chip-select-1 device up at all.
       Without it, a build with FORGIX_QSPI_PSRAM off is indistinguishable from a
       build that tried and failed: both report zero bytes and not ok. A caller
       that judges the result needs to know which, or it reports a fault for a
       device deliberately left alone. */
    bool psram_enabled;
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




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


bsp_memory_report_t BSP_MemoryCheck( void );

#ifdef __cplusplus
}
#endif

#endif
