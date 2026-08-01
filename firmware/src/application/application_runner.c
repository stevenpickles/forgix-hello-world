#include "application.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>

#include "bsp.h"

void application_run(void) {
    char line[128];
    size_t used = 0;
    while (true) {
        int character = bsp_console_getchar_timeout_us(1000);
        if (character == BSP_CONSOLE_TIMEOUT) {
            continue;
        }
        if (character == '\r' || character == '\n') {
            if (used) {
                line[used] = 0;
                application_process_command(line);
                used = 0;
            }
        } else if ((character == '\b' || character == 127) && used) {
            --used;
        } else if (isprint(character) && used + 1 < sizeof line) {
            line[used++] = (char)character;
        }
    }
}
