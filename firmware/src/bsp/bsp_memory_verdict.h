#ifndef FORGIX_BSP_MEMORY_VERDICT_H
#define FORGIX_BSP_MEMORY_VERDICT_H

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


/* Where to probe a freshly re-entered PSRAM window and with what, decided from
   the window size alone so the decision is host-testable. Two words at opposite
   ends with two distinct address-derived patterns: a bus that echoes the last
   write fails the first word, a stuck bus fails both, and gross address
   aliasing puts one word's pattern where the other's should be. */
typedef struct bsp_memory_probe_plan_t_tag
{
    bool viable;
    uint32_t first_word_index;
    uint32_t last_word_index;
    uint32_t first_pattern;
    uint32_t last_pattern;
} bsp_memory_probe_plan_t;


typedef enum bsp_memory_post_result_tag
{
    BSP_MEMORY_POST_PASS = 0,
    BSP_MEMORY_POST_NO_DEVICE,
    BSP_MEMORY_POST_KGD_FAIL,
    BSP_MEMORY_POST_DENSITY_FAIL,
    BSP_MEMORY_POST_MODE_REGISTER_FAIL,
    BSP_MEMORY_POST_SCRATCH_FAIL,
    BSP_MEMORY_POST_CONTROLLER_FAIL,
    BSP_MEMORY_POST_SKIPPED,
} bsp_memory_post_result;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


bsp_memory_probe_plan_t BSP_MemoryVerdictProbePlan( const uint32_t sizeBytes );

bool BSP_MemoryVerdictProbeHeld( const bsp_memory_probe_plan_t *const ptr_plan,
                                 const uint32_t observedFirst, const uint32_t observedLast );

bsp_memory_post_result BSP_MemoryVerdictPostClassify( uint8_t mfid, uint8_t kgd, uint8_t eid,
                                                      uint8_t mr0, bool scratchOk, bool restored );

uint8_t BSP_MemoryVerdictScratchByte( uint32_t address, bool inverted );

#ifdef __cplusplus
}
#endif

#endif
