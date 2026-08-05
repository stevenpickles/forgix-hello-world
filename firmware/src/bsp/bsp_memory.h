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
       parameters rather than by auto-detection -- at any point this boot, not
       on this call: forcing latches the SDK's initialised flag, so only the
       first check ever takes the forcing path, and a per-call answer would
       claim the device started auto-detecting between two reports. Detection
       only checks the identity byte, so a device that reports an unexpected
       vendor can still be perfectly good memory -- identity and function are
       separate questions. */
    bool psram_forced;
    /* Vendor known-good-die and device-ID bytes as the chip reported them over
       QSPI. Captured during the SDK's own detection at boot, and refreshed by
       every BSP_MemoryPsramIdentify call, which reads them in the datasheet's
       legal window. The boot capture alone is only trustworthy on a cold start:
       after a warm reboot the device is still in QPI from the previous session
       and the SDK's serial Read-ID returns nonsense. Both zero means neither
       path has run. */
    uint8_t psram_kgd;
    uint8_t psram_eid;
} bsp_memory_report_t;


/* The identity bytes read in the one window the datasheet allows: straight
   after a global reset. The read tears the device out of QPI, so the same call
   re-enters it before returning -- restored is whether that re-entry worked. */
typedef struct bsp_memory_psram_identity_t_tag
{
    uint8_t kgd; /* byte 5 of the Read-ID response */
    uint8_t eid; /* byte 6 */
    /* False means the memory window is down until the next successful call;
       nothing else in the firmware stores data there, so the failure is inert,
       but the caller should say so rather than report a working memory. */
    bool restored;
} bsp_memory_psram_identity_t;


enum
{
    /* One sweep pass's worth of traffic: 64 KiB is a few milliseconds through
       the uncached window, far inside the foreground poll contract and the
       watchdog period. */
    BSP_MEMORY_PSRAM_SWEEP_CHUNK_BYTES = 64 * 1024,
};


enum
{
    /* Read-ID as one 128-clock transfer: opcode, three address bytes the
       datasheet ignores, then twelve response bytes -- four past the full
       MF + KGD + 45-bit EID, so the capture shows whether the device keeps
       driving, repeats its ID, or goes quiet once the documented bytes are
       out. The production probe in BSP_MemoryPsramIdentify keeps its own
       shorter, tCEM-compliant transfer. */
    BSP_MEMORY_IDENTITY_RESPONSE_BYTES = 16,
    /* The production rate plus two slower confirmations. */
    BSP_MEMORY_IDENTITY_PROBE_RATES = 3,
};


/* Raw material for the identity investigation: every byte of the Read-ID
   response, from both chip selects, at several bus clocks. The flash on chip
   select 0 is the control -- a known device read through the same direct-mode
   engine at the same clock and sample settings, so a correct flash ID
   exonerates the controller's sampling. Identical PSRAM bytes across a 25x
   clock spread then rule out marginal timing on the device side. Reading the
   PSRAM identity resets the device, so the call re-enters QPI before
   returning. */
typedef struct bsp_memory_identity_dump_t_tag
{
    uint8_t flash_response[ BSP_MEMORY_IDENTITY_RESPONSE_BYTES ];
    /* False when the image was built without PSRAM support: the chip-select-1
       probes did not run and the bytes and rates below are meaningless. */
    bool psram_probed;
    uint32_t probe_hz[ BSP_MEMORY_IDENTITY_PROBE_RATES ];
    uint8_t psram_response[ BSP_MEMORY_IDENTITY_PROBE_RATES ][ BSP_MEMORY_IDENTITY_RESPONSE_BYTES ];
    /* False means the memory window is down until the next successful
       identify, exactly as bsp_memory_psram_identity_t reports it. */
    bool restored;
} bsp_memory_identity_dump_t;


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
       check. A chunk index past the device's size, or any chunk while the
       PSRAM is unavailable, fails with fail_address 0 rather than touching
       an unbacked window. */
    bool ok;
    uint32_t fail_address; /* uncached-alias address of the first mismatch, else 0 */
} bsp_memory_sweep_result_t;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


bsp_memory_report_t BSP_MemoryCheck( void );

/* Global reset, Read-ID in the legal window, then QPI re-entry, all in one call
   so an abort can never leave the device reset but not re-initialised. Costs a
   few bus transactions with interrupts briefly off; safe to call every run. */
bsp_memory_psram_identity_t BSP_MemoryPsramIdentify( void );

/* One chunk of one sweep pass. Chunk state lives with the caller, so the BSP
   holds nothing that can go stale if a run is aborted between chunks. */
bsp_memory_sweep_result_t BSP_MemoryPsramSweepChunk( bsp_memory_sweep_op op, uint32_t chunk_index );

/* The identity investigation behind the `memid` command: the full Read-ID
   response from the boot flash as a sampling control, then from the PSRAM at
   the production clock and two slower ones. Every extended read deliberately
   holds chip select past tCEM -- the ID register is static logic with no
   refresh dependency, and the array contents are rewritten by the next sweep
   -- so their only cost is to the DRAM data nobody is keeping. */
bsp_memory_identity_dump_t BSP_MemoryIdentityDump( void );

#ifdef __cplusplus
}
#endif

#endif
