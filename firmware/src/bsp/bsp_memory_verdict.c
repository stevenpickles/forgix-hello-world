/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_memory_verdict.h"




/***************************************************************************************
**
** Compiler Define Directives
**
***************************************************************************************/


/* Same address-derived scramble the sweep uses: the pattern is the word's own
   byte offset xor a constant, so no two words in the window share one and
   neither all-zeros nor all-ones can occur for any offset a real window
   reaches. */
#define PROBE_PATTERN_SEED ( (uint32_t) 0x5a5a5a5au )

#define BYTES_PER_WORD ( (uint32_t) 4u )

#define POST_MFID_AP_MEMORY ( (uint8_t) 0x0du )
#define POST_KGD_PASS ( (uint8_t) 0x5du )
#define POST_EID_DENSITY_MASK ( (uint8_t) 0xe0u )
#define POST_MR0_DOCUMENTED_MASK ( (uint8_t) 0x63u )
#define POST_MR0_RESET_VALUE ( (uint8_t) 0x60u )




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Plans the two-word verification probe from the window size alone. The
///     first and last words are the two addresses a wrong chip-select decode or
///     a truncated window are most likely to disagree about, and a window too
///     small for two distinct words cannot be probed meaningfully at all.
/// </summary>
/// <returns>
///     The plan, with viable false when the window cannot host two words.
/// </returns>
bsp_memory_probe_plan_t BSP_MemoryVerdictProbePlan( const uint32_t sizeBytes )
{
    bsp_memory_probe_plan_t plan = { 0 };

    if ( sizeBytes < ( 2u * BYTES_PER_WORD ) )
    {
        return plan;
    }

    plan.viable = true;
    plan.first_word_index = 0u;
    plan.last_word_index = ( sizeBytes / BYTES_PER_WORD ) - 1u;
    plan.first_pattern = ( plan.first_word_index * BYTES_PER_WORD ) ^ PROBE_PATTERN_SEED;
    plan.last_pattern = ( plan.last_word_index * BYTES_PER_WORD ) ^ PROBE_PATTERN_SEED;
    return plan;
}

/// <summary>
///     Judges the readback. Both words must hold exactly their planned patterns:
///     one wrong word is enough to call the window broken, because there is no
///     degree of "mostly works" a caller could safely store data in.
/// </summary>
/// <returns>
///     True when the probe proves the window read back what was written.
/// </returns>
bool BSP_MemoryVerdictProbeHeld( const bsp_memory_probe_plan_t *const ptr_plan,
                                 const uint32_t observedFirst, const uint32_t observedLast )
{
    return ptr_plan->viable && ( observedFirst == ptr_plan->first_pattern ) &&
           ( observedLast == ptr_plan->last_pattern );
}


/// <summary>
///     Classifies the POST in bus order so the result names the first invalid
///     observation rather than a downstream consequence of it. Reserved MR0
///     and manufacturing-ID bits are deliberately excluded from comparisons.
/// </summary>
/// <returns>
///     The first failure, or PASS when every check held.
/// </returns>
bsp_memory_post_result BSP_MemoryVerdictPostClassify( const uint8_t mfid, const uint8_t kgd,
                                                      const uint8_t eid, const uint8_t mr0,
                                                      const bool scratchOk, const bool restored )
{
    if ( mfid != POST_MFID_AP_MEMORY )
    {
        return BSP_MEMORY_POST_NO_DEVICE;
    }
    if ( kgd != POST_KGD_PASS )
    {
        return BSP_MEMORY_POST_KGD_FAIL;
    }
    if ( ( eid & POST_EID_DENSITY_MASK ) != 0u )
    {
        return BSP_MEMORY_POST_DENSITY_FAIL;
    }
    if ( ( mr0 & POST_MR0_DOCUMENTED_MASK ) != POST_MR0_RESET_VALUE )
    {
        return BSP_MEMORY_POST_MODE_REGISTER_FAIL;
    }
    if ( !scratchOk )
    {
        return BSP_MEMORY_POST_SCRATCH_FAIL;
    }
    if ( !restored )
    {
        return BSP_MEMORY_POST_CONTROLLER_FAIL;
    }
    return BSP_MEMORY_POST_PASS;
}


/// <summary>
///     Derives one scratch byte from its absolute address, optionally inverted,
///     so a misplaced write cannot agree with the expected byte by accident.
/// </summary>
/// <returns>
///     The byte to write and later verify.
/// </returns>
uint8_t BSP_MemoryVerdictScratchByte( const uint32_t address, const bool inverted )
{
    const uint8_t pattern = (uint8_t) ( address & 0xffu );
    return inverted ? (uint8_t) ~pattern : pattern;
}
