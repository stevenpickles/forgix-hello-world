/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application.h"

#include "application_diagnostics.h"
#include "application_ui.h"
#include "bsp.h"




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     The foreground loop, and it never returns. The order inside it is the
///     contract: diagnostics first, because that is where the watchdog is fed
///     and the progress marker is set, then USB service, then the UI poll, which
///     is the only step that blocks. Anything added here spends its time out of
///     the watchdog window, so it belongs inside a step rather than beside them.
/// </summary>
void application_run( void )
{
    application_diagnostics_start();
    application_ui_start();
    while ( true )
    {
        application_diagnostics_poll();
        BSP_UsbService();
        application_ui_poll();
    }
}
