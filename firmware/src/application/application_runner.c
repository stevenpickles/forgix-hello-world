#include "application.h"

#include "application_console.h"
#include "application_diagnostics.h"
#include "bsp.h"

void application_run(void) {
    application_diagnostics_start();
    application_console_start();
    while (true) {
        application_diagnostics_poll();
        BSP_UsbService();
        application_console_poll();
    }
}
