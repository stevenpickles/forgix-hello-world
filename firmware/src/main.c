/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application.h"
#include "bsp.h"




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Entry point of the USB console image, and the whole of it: bring the
///     board up, hand the result to the application so the boot report can say
///     what start-up found, then surrender the thread to the foreground loop.
///     No policy lives here -- an image differs from its siblings by which
///     application it links, not by what its main does.
/// </summary>
/// <returns>
///     Nothing, ever. application_run does not come back, so the int is here
///     because the language and the linker require it.
/// </returns>
int main( void )
{
    bsp_init_result_t bsp_result = BSP_Init();
    application_init( &bsp_result );
    application_run();
}
