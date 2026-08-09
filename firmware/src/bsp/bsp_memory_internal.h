#ifndef FORGIX_BSP_MEMORY_INTERNAL_H
#define FORGIX_BSP_MEMORY_INTERNAL_H

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
** Public Function Declarations
**
***************************************************************************************/


/* The seam between bsp_memory.c, which owns the chip-select-1 bring-up and the
   state BSP_MemoryCheck reports, and bsp_memory_identity.c, which owns the
   direct-mode Read-ID probes. Every probe resets the device out of QPI, so it
   has to hand the window back to the core rather than reconstruct it. Nothing
   here belongs to the BSP's callers: the _internal.h suffix keeps this header
   out of the bsp.h umbrella deliberately, so the pair below is reachable only
   from the two files that implement bsp_memory.h between them. */

#if FORGIX_QSPI_PSRAM
/* Configures chip select 1 from the datasheet's parameters instead of from what
   the device claims to be, brings it up, and proves the mapped window with an
   uncached readback before reporting success. Safe to call repeatedly; that is
   how a probe undoes the reset it just performed. */
bool BSP_MemoryPsramForceFromDatasheet( void );

/* Replaces the identity bytes the SDK's boot-time detection captured with a
   pair read in the datasheet's legal window. The core keeps the storage because
   it is what BSP_MemoryCheck reports; the probes supply the only reading that
   is trustworthy after a warm reboot. */
void BSP_MemoryPsramRecordIdentity( uint8_t kgd, uint8_t eid );
#endif

#ifdef __cplusplus
}
#endif

#endif
