#include "application.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsp.h"

static bool parse_byte(const char *text, uint8_t *value) {
    char *end = NULL;
    long parsed = strtol(text, &end, 0);
    if (!text[0] || !end || *end || parsed < 0 || parsed > 255) {
        return false;
    }
    *value = (uint8_t)parsed;
    return true;
}

static void print_help(void) {
    bsp_console_puts("hello | color <r> <g> <b> [brightness] | off | status | reset | help");
}

void application_process_command(char *line) {
    char *argv[6] = {0};
    int argc = 0;
    for (char *part = strtok(line, " \t"); part && argc < 6; part = strtok(NULL, " \t")) {
        argv[argc++] = part;
    }

    if (!argc) {
        return;
    }
    if (!strcmp(argv[0], "help") && argc == 1) {
        print_help();
        return;
    }
    if (!bsp_fpga_is_ready()) {
        bsp_console_puts("error: FPGA is not configured and responding; reset the board to retry");
        return;
    }

    if (!strcmp(argv[0], "hello") && argc == 1) {
        bsp_led_set(0, 255, 255, 64);
        uint8_t id = bsp_fpga_ping();
        bsp_led_state_t led = bsp_led_get();
        if (id == BSP_FPGA_DESIGN_ID && led.red == 0 && led.green == 255 &&
                led.blue == 255 && led.brightness == 64 && led.enabled) {
            bsp_console_printf("Hello from RP2354 -> FPGA %02X\n", id);
        } else {
            bsp_console_printf(
                "error: hello readback failed: id=%02X rgb=%u,%u,%u brightness=%u enable=%u\n",
                id, led.red, led.green, led.blue, led.brightness, led.enabled);
        }
    } else if (!strcmp(argv[0], "color") && (argc == 4 || argc == 5)) {
        uint8_t values[4] = {0, 0, 0, 255};
        bool valid = true;
        for (int index = 1; index < argc; ++index) {
            valid &= parse_byte(argv[index], &values[index - 1]);
        }
        if (!valid) {
            bsp_console_puts("error: values must be 0..255");
            return;
        }
        bsp_led_set(values[0], values[1], values[2], values[3]);
        bsp_console_puts("ok");
    } else if (!strcmp(argv[0], "off") && argc == 1) {
        bsp_led_off();
        bsp_console_puts("ok");
    } else if (!strcmp(argv[0], "status") && argc == 1) {
        bsp_button_state_t button = bsp_button_get_state();
        bsp_console_printf("id=%02X status=%02X button=%02X count=%u fpga_status=%u\n",
                           bsp_fpga_ping(), bsp_fpga_read_status(), button.level,
                           button.count, bsp_fpga_status_pin());
    } else if (!strcmp(argv[0], "reset") && argc == 1) {
        bsp_fpga_reset();
        bsp_console_puts("ok");
    } else {
        bsp_console_puts("error: invalid command (try help)");
    }
}

void application_init(const bsp_init_result_t *bsp_result) {
    bsp_console_printf("Forgix: configuration=%s design_id=%02X runtime=%s cdone=%u status=%u\n",
                       bsp_result->configured ? "ok" : "failed", bsp_result->design_id,
                       bsp_result->ready ? "ready" : "unavailable", bsp_result->cdone,
                       bsp_result->status_pin);
    if (!bsp_result->ready) {
        bsp_console_puts(
            "error: FPGA configuration or design-ID validation failed; runtime commands are disabled");
    }
    print_help();
}
