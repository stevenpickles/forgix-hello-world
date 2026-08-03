#include "application.h"

#include "application_diagnostics.h"
#include "application_ui.h"
#include "bsp.h"

void application_run(void) {
    application_diagnostics_start();
    application_ui_start();
    while (true) {
        application_diagnostics_poll();
        BSP_UsbService();
        application_ui_poll();
    }
}
