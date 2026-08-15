/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "application_memtest.h"
#include "bsp_memory_bist_logic.h"
#include "mock_bsp_console.h"
#include "mock_bsp_watchdog.h"
#include "mock_auto_bsp_memory_bist.h"




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static const application_activity_t *activity( void );

static bsp_memory_bist_status_t running_status( void );

static bsp_memory_bist_status_t finished_status( bsp_memory_bist_result result );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


void setUp( void )
{
    MOCK_BSP_ConsoleReset();
    MOCK_BSP_WatchdogReset();
}


void tearDown( void )
{
}


void test_start_warns_that_the_test_is_destructive_and_abortable( void )
{
    BSP_MemoryBistStart_Expect();

    activity()->start();

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "destructive" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "any key aborts" ) );
}


void test_poll_stays_silent_between_sweep_boundaries( void )
{
    BSP_MemoryBistStep_ExpectAndReturn( running_status() );

    TEST_ASSERT_TRUE( activity()->poll() );

    TEST_ASSERT_EQUAL_STRING( "", MOCK_BSP_ConsoleOutput() );
}


void test_poll_prints_one_progress_line_per_completed_sweep( void )
{
    bsp_memory_bist_status_t status = running_status();
    status.sweep_boundary = true;
    status.pair = 0;
    status.verifying = false;
    status.error_total = 0;
    BSP_MemoryBistStep_ExpectAndReturn( status );

    TEST_ASSERT_TRUE( activity()->poll() );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "memtest: [ 1/23] addr" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "write done" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "errors=0" ) );
}


void test_poll_names_the_verify_half_of_a_pair( void )
{
    bsp_memory_bist_status_t status = running_status();
    status.sweep_boundary = true;
    status.pair = 4;
    status.verifying = true;
    status.error_total = 2;
    BSP_MemoryBistStep_ExpectAndReturn( status );

    TEST_ASSERT_TRUE( activity()->poll() );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "memtest: [ 5/23] aa" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "verify done" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "errors=2" ) );
}


void test_a_passed_run_reports_the_cleared_memory_and_retires( void )
{
    bsp_memory_bist_status_t status = finished_status( BSP_MEMORY_BIST_PASS );
    status.sweep_boundary = true;
    status.pair = BSP_MEMORY_BIST_PAIRS - 1u;
    status.verifying = true;
    BSP_MemoryBistStep_ExpectAndReturn( status );

    TEST_ASSERT_FALSE( activity()->poll() );

    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "[23/23] clear verify done" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "memtest: pass errors=0 restored=1" ) );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "memory left cleared to 0x00" ) );
}


void test_a_failed_run_prints_the_fault_map_and_the_uncounted_tail( void )
{
    bsp_memory_bist_status_t status = finished_status( BSP_MEMORY_BIST_DATA_FAIL );
    status.error_total = 5;
    status.log.total = 5;
    status.log.recorded = 2;
    /* positional: pair, page, address, expected, actual, xor */
    status.log.records[ 0 ] = ( bsp_memory_bist_error_t ){ 0, 3, 0x000603u, 0x5Au, 0x4Au, 0x10u };
    status.log.records[ 1 ] =
        ( bsp_memory_bist_error_t ){ 4, 4095, 0x1FFFFFu, 0xAAu, 0x00u, 0xAAu };
    BSP_MemoryBistStep_ExpectAndReturn( status );

    TEST_ASSERT_FALSE( activity()->poll() );

    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(), "memtest: data-fail errors=5 restored=1" ) );
    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(),
                "memtest: err pair=0(addr) page=3 addr=000603 expected=5A actual=4A xor=10" ) );
    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(),
                "memtest: err pair=4(aa) page=4095 addr=1FFFFF expected=AA actual=00 xor=AA" ) );
    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(), "3 further errors counted beyond the 2 recorded" ) );
}


void test_alias_and_controller_verdicts_are_named_distinctly( void )
{
    BSP_MemoryBistStep_ExpectAndReturn( finished_status( BSP_MEMORY_BIST_ALIAS_FAIL ) );
    TEST_ASSERT_FALSE( activity()->poll() );
    TEST_ASSERT_NOT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "memtest: alias-fail" ) );
    MOCK_BSP_ConsoleReset();

    bsp_memory_bist_status_t status = finished_status( BSP_MEMORY_BIST_CONTROLLER_FAIL );
    status.restored = false;
    BSP_MemoryBistStep_ExpectAndReturn( status );
    TEST_ASSERT_FALSE( activity()->poll() );
    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(), "memtest: controller-fail errors=0 restored=0" ) );
}


void test_a_build_without_psram_says_so_instead_of_printing_bytes( void )
{
    BSP_MemoryBistStep_ExpectAndReturn( finished_status( BSP_MEMORY_BIST_SKIPPED ) );

    TEST_ASSERT_FALSE( activity()->poll() );

    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(), "not enabled in this build (FORGIX_QSPI_PSRAM off)" ) );
    TEST_ASSERT_NULL( strstr( MOCK_BSP_ConsoleOutput(), "errors=" ) );
}


void test_abort_reports_whether_the_memory_came_back( void )
{
    BSP_MemoryBistAbort_ExpectAndReturn( true );
    activity()->stop();
    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(), "aborted; memory window remains mapped" ) );
    MOCK_BSP_ConsoleReset();

    BSP_MemoryBistAbort_ExpectAndReturn( false );
    activity()->stop();
    TEST_ASSERT_NOT_NULL(
        strstr( MOCK_BSP_ConsoleOutput(), "error: memory window is unavailable" ) );
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


static const application_activity_t *activity( void )
{
    return application_memtest_activity();
}


static bsp_memory_bist_status_t running_status( void )
{
    bsp_memory_bist_status_t status;
    memset( &status, 0, sizeof status );
    status.done = false;
    return status;
}


static bsp_memory_bist_status_t finished_status( bsp_memory_bist_result result )
{
    bsp_memory_bist_status_t status;
    memset( &status, 0, sizeof status );
    status.done = true;
    status.result = result;
    status.restored = true;
    return status;
}
