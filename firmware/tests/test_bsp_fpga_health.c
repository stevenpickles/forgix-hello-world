/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "unity.h"

#include <stdbool.h>
#include <stdint.h>

#include "bsp_fpga.h"
#include "bsp_fpga_health.h"




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


void setUp( void )
{
    /* The latch is module state shared by every test in this file, so each test
       starts from the power-on truth: nothing has succeeded yet. */
    BSP_FpgaHealthSetReady( false );
}


void tearDown( void )
{
}


void test_ready_verdict_requires_configuration_and_the_expected_design_id( void )
{
    TEST_ASSERT_TRUE( BSP_FpgaHealthReadyVerdict( true, BSP_FPGA_DESIGN_ID ) );
    /* An unconfigured FPGA is not ready even if the bus happens to echo the
       right byte back -- CDONE never rose, so nothing is executing. */
    TEST_ASSERT_FALSE( BSP_FpgaHealthReadyVerdict( false, BSP_FPGA_DESIGN_ID ) );
    /* Zero is what a dead bus reads; the complement is a live bus answering as
       some other design. Both are equally unusable. */
    TEST_ASSERT_FALSE( BSP_FpgaHealthReadyVerdict( true, 0x00u ) );
    TEST_ASSERT_FALSE( BSP_FpgaHealthReadyVerdict( true, (uint8_t) ~BSP_FPGA_DESIGN_ID ) );
}


void test_readiness_defaults_to_not_ready( void )
{
    TEST_ASSERT_FALSE( BSP_FpgaHealthIsReady() );
}


void test_a_runtime_failure_clears_a_ready_latch( void )
{
    BSP_FpgaHealthSetReady( true );
    TEST_ASSERT_TRUE( BSP_FpgaHealthIsReady() );

    BSP_FpgaHealthSetReady( false );
    TEST_ASSERT_FALSE( BSP_FpgaHealthIsReady() );
}


void test_a_successful_bring_up_restores_readiness_after_a_failure( void )
{
    BSP_FpgaHealthSetReady( true );
    BSP_FpgaHealthSetReady( false );

    /* The reconfiguration path runs the same bring-up as boot, so a success
       writes true through the same call and the latch must come back. */
    BSP_FpgaHealthSetReady( BSP_FpgaHealthReadyVerdict( true, BSP_FPGA_DESIGN_ID ) );
    TEST_ASSERT_TRUE( BSP_FpgaHealthIsReady() );
}
