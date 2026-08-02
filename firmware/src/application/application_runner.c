#include "application.h"

#include "application_console.h"

void application_run(void) {
    application_console_start();
    while (true) {
        application_console_poll();
    }
}
