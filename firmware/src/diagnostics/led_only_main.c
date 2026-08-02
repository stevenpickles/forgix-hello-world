/* USB-free control image for the lockup investigation.
 *
 * Restored from the feature/5 diagnostic stash and instrumented: it runs the
 * same application diagnostics poll as the shell firmware (watchdog, progress
 * markers, FPGA health check with auto-reconfigure, LED boot blink report) but
 * links the bsp_usb stub, so no USB code is compiled in. This is the primary
 * instrument for the LED-only freeze that appears after ~45-75 minutes on every
 * power source.
 *
 * Note that stdio is disabled for this target, so the boot report reaches the
 * operator only as an LED blink code.
 */
#include "application_diagnostics.h"
#include "bsp.h"

int main(void) {
    /* Same board bring-up as the shell image: deselects the QSPI device on chip
       select 1 before anything else, then brings up stdio (UART only when built
       with FORGIX_DIAGNOSTIC_UART) and configures the FPGA. */
    (void)bsp_init();
    application_diagnostics_start();

    while (true) {
        application_diagnostics_poll();
    }
}
