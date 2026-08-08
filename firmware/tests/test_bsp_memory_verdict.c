/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "unity.h"

#include <stdbool.h>
#include <stdint.h>

#include "bsp_memory_verdict.h"




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


void setUp( void )
{
}


void tearDown( void )
{
}


void test_probe_plan_targets_both_ends_of_the_window_with_distinct_patterns( void )
{
    const bsp_memory_probe_plan_t plan = BSP_MemoryVerdictProbePlan( 2u * 1024u * 1024u );

    TEST_ASSERT_TRUE( plan.viable );
    TEST_ASSERT_EQUAL_UINT32( 0u, plan.first_word_index );
    TEST_ASSERT_EQUAL_UINT32( 524287u, plan.last_word_index );
    TEST_ASSERT_NOT_EQUAL( plan.first_pattern, plan.last_pattern );
    /* Neither pattern may be a stuck-bus value, or a dead window could pass. */
    TEST_ASSERT_NOT_EQUAL( 0x00000000u, plan.first_pattern );
    TEST_ASSERT_NOT_EQUAL( 0xffffffffu, plan.first_pattern );
    TEST_ASSERT_NOT_EQUAL( 0x00000000u, plan.last_pattern );
    TEST_ASSERT_NOT_EQUAL( 0xffffffffu, plan.last_pattern );
}


void test_probe_plan_refuses_a_window_too_small_for_two_words( void )
{
    TEST_ASSERT_FALSE( BSP_MemoryVerdictProbePlan( 0u ).viable );
    TEST_ASSERT_FALSE( BSP_MemoryVerdictProbePlan( 4u ).viable );
    TEST_ASSERT_FALSE( BSP_MemoryVerdictProbePlan( 7u ).viable );

    const bsp_memory_probe_plan_t smallest = BSP_MemoryVerdictProbePlan( 8u );
    TEST_ASSERT_TRUE( smallest.viable );
    TEST_ASSERT_EQUAL_UINT32( 0u, smallest.first_word_index );
    TEST_ASSERT_EQUAL_UINT32( 1u, smallest.last_word_index );
}


void test_probe_held_requires_both_words( void )
{
    const bsp_memory_probe_plan_t plan = BSP_MemoryVerdictProbePlan( 1024u );

    TEST_ASSERT_TRUE( BSP_MemoryVerdictProbeHeld( &plan, plan.first_pattern, plan.last_pattern ) );
    TEST_ASSERT_FALSE(
        BSP_MemoryVerdictProbeHeld( &plan, plan.first_pattern ^ 1u, plan.last_pattern ) );
    TEST_ASSERT_FALSE(
        BSP_MemoryVerdictProbeHeld( &plan, plan.first_pattern, plan.last_pattern ^ 1u ) );
}


void test_probe_held_rejects_a_bus_that_echoes_the_last_write( void )
{
    const bsp_memory_probe_plan_t plan = BSP_MemoryVerdictProbePlan( 1024u );

    /* A bus with no memory behind it returns whatever was written last: both
       reads come back as the last pattern written, and the first word's slot is
       where that lie shows. */
    TEST_ASSERT_FALSE( BSP_MemoryVerdictProbeHeld( &plan, plan.last_pattern, plan.last_pattern ) );
}


void test_probe_held_rejects_a_stuck_bus( void )
{
    const bsp_memory_probe_plan_t plan = BSP_MemoryVerdictProbePlan( 1024u );

    TEST_ASSERT_FALSE( BSP_MemoryVerdictProbeHeld( &plan, 0x00000000u, 0x00000000u ) );
    TEST_ASSERT_FALSE( BSP_MemoryVerdictProbeHeld( &plan, 0xffffffffu, 0xffffffffu ) );
}


void test_probe_held_is_false_for_a_plan_that_was_never_viable( void )
{
    const bsp_memory_probe_plan_t plan = BSP_MemoryVerdictProbePlan( 0u );

    /* Even observations that happen to match a non-viable plan's zeroed
       patterns must not pass: the probe never ran. */
    TEST_ASSERT_FALSE(
        BSP_MemoryVerdictProbeHeld( &plan, plan.first_pattern, plan.last_pattern ) );
}
