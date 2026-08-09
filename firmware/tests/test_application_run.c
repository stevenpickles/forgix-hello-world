/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "unity.h"

#include <setjmp.h>
#include <stdint.h>

#include "application_runner.h"
#include "mock_bsp_usb.h"
#include "mock_auto_application_diagnostics.h"
#include "mock_auto_application_ui.h"




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


static jmp_buf loop_escape;

static uint32_t ui_poll_count;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static void ui_poll_hook( int cmock_num_calls );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


void setUp( void )
{
    MOCK_BSP_UsbReset();
    ui_poll_count = 0;
}


void tearDown( void )
{
}


/* The loop itself never returns, so the UI poll stub jumps back out after three
   passes. Everything the runner promises is counted: one start for each half of
   the application, then diagnostics, USB service, and UI poll once per pass. */
void test_the_foreground_loop_starts_both_halves_and_then_services_each_pass( void )
{
    application_diagnostics_start_Expect();
    application_ui_start_Expect();
    application_diagnostics_poll_Expect();
    application_diagnostics_poll_Expect();
    application_diagnostics_poll_Expect();
    application_ui_poll_Stub( ui_poll_hook );

    if ( setjmp( loop_escape ) == 0 )
    {
        application_run();
        TEST_FAIL_MESSAGE( "application_run returned, and it never may" );
    }

    TEST_ASSERT_EQUAL_UINT32( 3, ui_poll_count );
    TEST_ASSERT_EQUAL_UINT32( 3, MOCK_BSP_UsbServiceCount() );
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


static void ui_poll_hook( int cmock_num_calls )
{
    (void) cmock_num_calls;
    ++ui_poll_count;
    if ( ui_poll_count >= 3u )
    {
        longjmp( loop_escape, 1 );
    }
}
