#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpga_image.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

enum {
    PIN_CS = 1, PIN_SCK = 2, PIN_SDIO = 3, PIN_CRESET_N = 4,
    PIN_CDONE = 5, PIN_STATUS = 6, PIN_OSC_EN = 19,
};
enum { CMD_WRITE = 0x02, CMD_READ = 0x03, CMD_RESET = 0x7f, CMD_PING = 0x9f };
enum {
    DESIGN_ID = 0xb5, REG_STATUS = 0x01, REG_LED_R = 0x10, REG_LED_G = 0x11,
    REG_LED_B = 0x12, REG_LED_GLOBAL = 0x13, REG_LED_ENABLE = 0x14,
    REG_BUTTON = 0x20, REG_BUTTON_COUNT = 0x21,
};

static bool fpga_ready;

static void runtime_bus_idle(void) {
    spi_deinit(spi0);
    gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT); gpio_put(PIN_CS, 1);
    gpio_init(PIN_SCK); gpio_set_dir(PIN_SCK, GPIO_OUT); gpio_put(PIN_SCK, 0);
    gpio_init(PIN_SDIO); gpio_set_dir(PIN_SDIO, GPIO_IN);
}

static bool configure_fpga(void) {
    gpio_init(PIN_OSC_EN); gpio_set_dir(PIN_OSC_EN, GPIO_OUT); gpio_put(PIN_OSC_EN, 1);
    gpio_init(PIN_CRESET_N); gpio_set_dir(PIN_CRESET_N, GPIO_OUT); gpio_put(PIN_CRESET_N, 0);
    gpio_init(PIN_CDONE); gpio_set_dir(PIN_CDONE, GPIO_IN);
    gpio_init(PIN_STATUS); gpio_set_dir(PIN_STATUS, GPIO_IN);
    gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT); gpio_put(PIN_CS, 0);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SDIO, GPIO_FUNC_SPI);
    spi_init(spi0, 8 * 1000 * 1000);
    spi_set_format(spi0, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    sleep_ms(2); gpio_put(PIN_CRESET_N, 1); sleep_ms(5);
    spi_write_blocking(spi0, fpga_image, fpga_image_size);
    const uint8_t trailing[32] = {0};
    spi_write_blocking(spi0, trailing, sizeof trailing);
    absolute_time_t deadline = make_timeout_time_ms(500);
    bool done = false;
    while (!time_reached(deadline)) {
        if (gpio_get(PIN_CDONE)) { done = true; break; }
        sleep_ms(1);
    }
    gpio_put(PIN_CS, 1);
    runtime_bus_idle();
    return done;
}

static void send_byte(uint8_t value, bool release_after_sample) {
    gpio_set_dir(PIN_SDIO, GPIO_OUT);
    for (int bit = 7; bit >= 0; --bit) {
        gpio_put(PIN_SDIO, (value >> bit) & 1u);
        gpio_put(PIN_SCK, 1);
        busy_wait_us_32(1);
        if (release_after_sample && bit == 0) gpio_set_dir(PIN_SDIO, GPIO_IN);
        gpio_put(PIN_SCK, 0);
        busy_wait_us_32(1);
    }
}

static uint8_t receive_byte(void) {
    uint8_t value = 0;
    gpio_set_dir(PIN_SDIO, GPIO_IN);
    busy_wait_us_32(1);
    for (int bit = 0; bit < 8; ++bit) {
        gpio_put(PIN_SCK, 1); busy_wait_us_32(1);
        value = (uint8_t)((value << 1) | gpio_get(PIN_SDIO));
        gpio_put(PIN_SCK, 0); busy_wait_us_32(1);
    }
    return value;
}

static uint8_t transaction(const uint8_t *tx, size_t count, bool read) {
    gpio_put(PIN_CS, 0); busy_wait_us_32(1);
    for (size_t i = 0; i < count; ++i) send_byte(tx[i], read && i + 1 == count);
    uint8_t result = read ? receive_byte() : 0;
    gpio_put(PIN_CS, 1); gpio_set_dir(PIN_SDIO, GPIO_IN); busy_wait_us_32(1);
    return result;
}

static uint8_t ping(void) { const uint8_t tx[] = {CMD_PING}; return transaction(tx, 1, true); }
static uint8_t read_reg(uint8_t reg) { const uint8_t tx[] = {CMD_READ, reg}; return transaction(tx, 2, true); }
static void write_reg(uint8_t reg, uint8_t value) { const uint8_t tx[] = {CMD_WRITE, reg, value}; transaction(tx, 3, false); }

static bool parse_byte(const char *text, uint8_t *value) {
    char *end = NULL; long parsed = strtol(text, &end, 0);
    if (!text[0] || !end || *end || parsed < 0 || parsed > 255) return false;
    *value = (uint8_t)parsed; return true;
}

static void print_help(void) {
    puts("hello | color <r> <g> <b> [brightness] | off | status | reset | help");
}

static void execute(char *line) {
    char *argv[6] = {0}; int argc = 0;
    for (char *p = strtok(line, " \t"); p && argc < 6; p = strtok(NULL, " \t")) argv[argc++] = p;
    if (!argc) return;
    if (!strcmp(argv[0], "help") && argc == 1) {
        print_help();
        return;
    }
    if (!fpga_ready) {
        puts("error: FPGA is not configured and responding; reset the board to retry");
        return;
    }
    if (!strcmp(argv[0], "hello") && argc == 1) {
        write_reg(REG_LED_R, 0); write_reg(REG_LED_G, 255); write_reg(REG_LED_B, 255);
        write_reg(REG_LED_GLOBAL, 64); write_reg(REG_LED_ENABLE, 1);
        uint8_t id = ping();
        uint8_t red = read_reg(REG_LED_R);
        uint8_t green = read_reg(REG_LED_G);
        uint8_t blue = read_reg(REG_LED_B);
        uint8_t brightness = read_reg(REG_LED_GLOBAL);
        uint8_t enabled = read_reg(REG_LED_ENABLE);
        if (id == DESIGN_ID && red == 0 && green == 255 && blue == 255 &&
                brightness == 64 && enabled == 1) {
            printf("Hello from RP2354 -> FPGA %02X\n", id);
        } else {
            printf("error: hello readback failed: id=%02X rgb=%u,%u,%u brightness=%u enable=%u\n",
                   id, red, green, blue, brightness, enabled);
        }
    } else if (!strcmp(argv[0], "color") && (argc == 4 || argc == 5)) {
        uint8_t v[4] = {0, 0, 0, 255}; bool valid = true;
        for (int i = 1; i < argc; ++i) valid &= parse_byte(argv[i], &v[i - 1]);
        if (!valid) { puts("error: values must be 0..255"); return; }
        write_reg(REG_LED_R, v[0]); write_reg(REG_LED_G, v[1]); write_reg(REG_LED_B, v[2]);
        write_reg(REG_LED_GLOBAL, v[3]); write_reg(REG_LED_ENABLE, 1); puts("ok");
    } else if (!strcmp(argv[0], "off") && argc == 1) {
        write_reg(REG_LED_ENABLE, 0); puts("ok");
    } else if (!strcmp(argv[0], "status") && argc == 1) {
        printf("id=%02X status=%02X button=%02X count=%u fpga_status=%u\n", ping(),
               read_reg(REG_STATUS), read_reg(REG_BUTTON), read_reg(REG_BUTTON_COUNT), gpio_get(PIN_STATUS));
    } else if (!strcmp(argv[0], "reset") && argc == 1) {
        const uint8_t tx[] = {CMD_RESET}; transaction(tx, 1, false); puts("ok");
    } else puts("error: invalid command (try help)");
}

int main(void) {
    stdio_init_all();
    bool configured = configure_fpga();
    sleep_ms(1500);
    uint8_t id = configured ? ping() : 0;
    fpga_ready = configured && id == DESIGN_ID;
    printf("Forgix: configuration=%s design_id=%02X runtime=%s cdone=%u status=%u\n",
           configured ? "ok" : "failed", id, fpga_ready ? "ready" : "unavailable",
           gpio_get(PIN_CDONE), gpio_get(PIN_STATUS));
    if (!fpga_ready) {
        puts("error: FPGA configuration or design-ID validation failed; runtime commands are disabled");
    }
    print_help();
    char line[128]; size_t used = 0;
    while (true) {
        int ch = getchar_timeout_us(1000);
        if (ch == PICO_ERROR_TIMEOUT) continue;
        if (ch == '\r' || ch == '\n') {
            if (used) { line[used] = 0; execute(line); used = 0; }
        } else if ((ch == '\b' || ch == 127) && used) --used;
        else if (isprint(ch) && used + 1 < sizeof line) line[used++] = (char)ch;
    }
}
