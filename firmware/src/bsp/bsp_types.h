#ifndef FORGIX_BSP_TYPES_H
#define FORGIX_BSP_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


/* The single point where the BSP pulls in the standard type definitions.
   Every bsp_*.h includes this rather than reaching for <stdint.h> or
   <stdbool.h> individually, so no header can declare an interface in terms of
   a type it forgot to pull in, and adding a type to the vocabulary is a change
   in one file rather than nine.

   The BSP API is written in fixed-width types throughout: a parameter or field
   states its width at the point of declaration, so a reader never has to know
   the target's word size to know what fits. uint8_t means a byte on a bus,
   uint32_t means a 32-bit register or millisecond count, int16_t means a value
   that carries a negative sentinel alongside a byte.

   size_t appears only where the C library forces it -- sizeof, strlen, and the
   psram_eid_to_size hook whose signature the Pico SDK fixes. Counts and indices
   the BSP owns are uint32_t. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
}
#endif

#endif
