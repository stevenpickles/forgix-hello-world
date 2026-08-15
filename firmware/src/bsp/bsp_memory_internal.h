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
** Enumerated Values, Type Definitions
**
***************************************************************************************/


#if FORGIX_QSPI_PSRAM
/* One direct-mode transfer. Every descriptor and byte buffer passed to the
   sequence functions below must live in SRAM: chip-select-0 XIP is unavailable
   while the engine dereferences them. */
typedef struct bsp_memory_cs_operation_t_tag
{
    const uint8_t *ptr_transmit;
    uint8_t *ptr_receive;
    uint32_t count;
    bool quad;
    uint32_t delay_cycles_after;
} bsp_memory_cs_operation_t;
#endif




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


/* The seam between the chip-select-1 bring-up, boot POST and SRAM-resident
   direct-mode engine. Nothing here belongs to BSP callers: the _internal.h
   suffix keeps the hardware mechanics out of the bsp.h umbrella. */

#if FORGIX_QSPI_PSRAM
/* Configures chip select 1 from the datasheet's parameters instead of from what
   the device claims to be, brings it up, and proves the mapped window with an
   uncached readback before reporting success. Safe to call repeatedly; that is
   how a probe undoes the reset it just performed. */
bool BSP_MemoryPsramForceFromDatasheet( void );

/* Replaces the SDK detection bytes with the boot POST's legal post-reset
   capture. The core keeps the storage because BSP_MemoryCheck reports it. */
void BSP_MemoryPsramRecordIdentity( uint8_t kgd, uint8_t eid );

/* Runs one atomic CS1 direct-mode window. The wrapper owns cache cleaning,
   interrupt masking, temporary ROM visibility of CS1, and the boot2 restore;
   callers only build an SRAM-resident operation list. Restoring the original
   CS1 size before boot2 is deliberate: boot2 must restore flash without
   sending an unsolicited exit sequence to the just-tested PSRAM. */
void BSP_MemoryCs1OperationSequence( const bsp_memory_cs_operation_t *ptr_operations,
                                     uint32_t operationCount, uint32_t clkdiv );

#endif

#ifdef __cplusplus
}
#endif

#endif
