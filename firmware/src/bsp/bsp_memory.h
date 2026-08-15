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


#include "bsp_memory_verdict.h"
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
       parameters rather than by auto-detection -- at any point this boot, not
       on this call: forcing latches the SDK's initialised flag, so only the
       first check ever takes the forcing path, and a per-call answer would
       claim the device started auto-detecting between two reports. Detection
       only checks the identity byte, so a device that reports an unexpected
       vendor can still be perfectly good memory -- identity and function are
       separate questions. Latched when the forced bring-up mapped the window,
       even if the readback verification then failed: it records "brought up by
       forcing", not "verified working". */
    bool psram_forced;
    /* Vendor known-good-die and device-ID bytes captured by the boot POST in
       the datasheet's legal post-reset window. Runtime diagnostics only read
       the cached capture and never put another Read-ID transaction on CS1.
       Both zero means the POST did not run. */
    uint8_t psram_kgd;
    uint8_t psram_eid;
} bsp_memory_report_t;


/* The identity bytes cached by the boot POST. The legacy function name remains
   because IBIT consumes this small view, but calling it never touches QSPI. */
typedef struct bsp_memory_psram_identity_t_tag
{
    uint8_t kgd; /* byte 5 of the Read-ID response */
    uint8_t eid; /* byte 6 */
    /* False means no verified window is advertised until the next successful
       call -- either re-entry failed before mapping anything, or the mapped
       window flunked the readback verification and its advertised size was
       zeroed. Nothing else in the firmware stores data there, so the failure
       costs the rest of the firmware nothing, but the caller should say so
       rather than report a working memory. */
    bool restored;
} bsp_memory_psram_identity_t;


/* Boot-only identity capture. Read-ID is legal only immediately after the
   global reset inside BSP_MemoryPsramPost; later callers retrieve this cached
   report and never put another 9Fh transaction on CS1. */
typedef struct bsp_memory_post_report_t_tag
{
    bsp_memory_post_result result;
    bool ran;
    uint8_t mfid;
    uint8_t kgd;
    uint8_t eid;
    uint8_t mr0;
    bool scratch_ok;
    uint32_t scratch_fail_address;
    bool restored;
} bsp_memory_post_report_t;


enum
{
    /* One sweep pass's worth of traffic: 64 KiB is a few milliseconds through
       the uncached window, far inside the foreground poll contract and the
       watchdog period. */
    BSP_MEMORY_PSRAM_SWEEP_CHUNK_BYTES = 64 * 1024,
};


/* The three passes of a moving-inversion sweep. Every chunk of a pass runs
   before any chunk of the next, which is what catches address aliasing: a
   smaller die answering a 2 MByte window puts an early chunk's pattern where a
   later chunk's is expected, and only a verify that starts after every write
   has landed can see that. */
typedef enum bsp_memory_sweep_op_tag
{
    BSP_MEMORY_SWEEP_WRITE = 0,      /* write the address-derived pattern */
    BSP_MEMORY_SWEEP_VERIFY_INVERT,  /* verify it, then write its inverse */
    BSP_MEMORY_SWEEP_VERIFY_INVERSE, /* verify the inverse */
} bsp_memory_sweep_op;


typedef struct bsp_memory_sweep_result_t_tag
{
    /* Write chunks pass when the chunk exists; verify chunks report the
       check. A chunk index past the advertised size, or any chunk while no
       PSRAM size is advertised at all, fails with fail_address 0 rather than
       touching an unbacked window. */
    bool ok;
    uint32_t fail_address; /* uncached-alias address of the first mismatch, else 0 */
} bsp_memory_sweep_result_t;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


bsp_memory_report_t BSP_MemoryCheck( void );

/* Runs once from BSP_Init, before USB is initialized. The complete reset and
   Read-ID sequence is one direct-mode window at 25 MHz. */
bsp_memory_post_report_t BSP_MemoryPsramPost( void );

/* Records that a watchdog caught the previous POST and contains CS1 by
   advertising no mapped size. Used only by BSP_Init's one-boot recovery path. */
bsp_memory_post_report_t BSP_MemoryPsramPostWatchdogRecovery( void );

/* Returns the boot capture without touching the QSPI bus. */
bsp_memory_post_report_t BSP_MemoryPsramPostReport( void );

/* Returns the boot POST's cached identity and restoration result without
   touching the QSPI bus. */
bsp_memory_psram_identity_t BSP_MemoryPsramIdentify( void );

/* One chunk of one sweep pass. Chunk state lives with the caller, so the BSP
   holds nothing that can go stale if a run is aborted between chunks. */
bsp_memory_sweep_result_t BSP_MemoryPsramSweepChunk( bsp_memory_sweep_op op, uint32_t chunk_index );

#ifdef __cplusplus
}
#endif

#endif
