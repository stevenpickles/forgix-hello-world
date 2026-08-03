#include "bsp_fpga.h"

#include <stddef.h>

#include "fpga_image.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

/* The physical pin assignment for the bit-banged link between the RP2350 and
   the FPGA: which GPIO carries chip select, clock, the shared data line, and
   the reset/handshake/oscillator-enable signals that bring the FPGA up. */
typedef enum
{
    PIN_CS = 1,
    PIN_SCK = 2,
    PIN_SDIO = 3,
    PIN_CRESET_N = 4,
    PIN_CDONE = 5,
    PIN_STATUS = 6,
    PIN_OSC_EN = 19,
} bsp_fpga_pin_t;

/* Opcodes understood by the FPGA's register protocol, sent as the first byte
   of a transaction to select whether the bytes that follow write a register,
   read one back, reset the design, or just ping it for its design ID. */
typedef enum
{
    CMD_WRITE = 0x02,
    CMD_READ = 0x03,
    CMD_RESET = 0x7f,
    CMD_PING = 0x9f,
} bsp_fpga_command_t;

/* The register the FPGA reports its own status through -- cdone/config state
   as seen from inside the design rather than off the CDONE pin. */
#define REG_STATUS ((uint8_t)0x01)

static bool fpga_ready;

static void runtime_bus_idle(void)
{
    spi_deinit(spi0);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    gpio_init(PIN_SCK);
    gpio_set_dir(PIN_SCK, GPIO_OUT);
    gpio_put(PIN_SCK, 0);
    gpio_init(PIN_SDIO);
    gpio_set_dir(PIN_SDIO, GPIO_IN);
}

static bool configure(void)
{
    gpio_init(PIN_OSC_EN);
    gpio_set_dir(PIN_OSC_EN, GPIO_OUT);
    gpio_put(PIN_OSC_EN, 1);
    gpio_init(PIN_CRESET_N);
    gpio_set_dir(PIN_CRESET_N, GPIO_OUT);
    gpio_put(PIN_CRESET_N, 0);
    gpio_init(PIN_CDONE);
    gpio_set_dir(PIN_CDONE, GPIO_IN);
    gpio_init(PIN_STATUS);
    gpio_set_dir(PIN_STATUS, GPIO_IN);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 0);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SDIO, GPIO_FUNC_SPI);
    spi_init(spi0, 8 * 1000 * 1000);
    spi_set_format(spi0, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    sleep_ms(2);
    gpio_put(PIN_CRESET_N, 1);
    sleep_ms(5);
    spi_write_blocking(spi0, fpga_image, fpga_image_size);

    const uint8_t trailing[32] = {0};
    spi_write_blocking(spi0, trailing, sizeof trailing);

    const absolute_time_t deadline = make_timeout_time_ms(500);
    bool done = false;
    while (!time_reached(deadline))
    {
        if (gpio_get(PIN_CDONE))
        {
            done = true;
            break;
        }
        sleep_ms(1);
    }

    gpio_put(PIN_CS, 1);
    runtime_bus_idle();
    return done;
}

static void send_byte(const uint8_t value, const bool release_after_sample)
{
    gpio_set_dir(PIN_SDIO, GPIO_OUT);
    for (int32_t bit = 7; bit >= 0; --bit)
    {
        gpio_put(PIN_SDIO, (value >> bit) & 1u);
        gpio_put(PIN_SCK, 1);
        busy_wait_us_32(1);
        if (release_after_sample && bit == 0)
        {
            gpio_set_dir(PIN_SDIO, GPIO_IN);
        }
        gpio_put(PIN_SCK, 0);
        busy_wait_us_32(1);
    }
}

static uint8_t receive_byte(void)
{
    uint8_t value = 0;
    gpio_set_dir(PIN_SDIO, GPIO_IN);
    busy_wait_us_32(1);
    for (uint32_t bit = 0; bit < 8; ++bit)
    {
        gpio_put(PIN_SCK, 1);
        busy_wait_us_32(1);
        value = (uint8_t)((value << 1) | gpio_get(PIN_SDIO));
        gpio_put(PIN_SCK, 0);
        busy_wait_us_32(1);
    }
    return value;
}

static uint8_t transaction(const uint8_t *const tx, const uint32_t count, const bool read)
{
    gpio_put(PIN_CS, 0);
    busy_wait_us_32(1);
    for (uint32_t index = 0; index < count; ++index)
    {
        send_byte(tx[index], read && index + 1 == count);
    }
    uint8_t result = 0;
    if (read)
    {
        result = receive_byte();
    }
    gpio_put(PIN_CS, 1);
    gpio_set_dir(PIN_SDIO, GPIO_IN);
    busy_wait_us_32(1);
    return result;
}

bsp_fpga_init_result_t BSP_FpgaInit(void)
{
    bsp_fpga_init_result_t result = {0};
    result.configured = configure();

    sleep_ms(1500);
    if (result.configured)
    {
        result.design_id = BSP_FpgaPing();
    }
    else
    {
        result.design_id = 0;
    }
    result.ready = result.configured && result.design_id == BSP_FPGA_DESIGN_ID;
    result.cdone = gpio_get(PIN_CDONE);
    result.status_pin = gpio_get(PIN_STATUS);
    fpga_ready = result.ready;
    return result;
}

bool BSP_FpgaReconfigure(void)
{
    const bsp_fpga_init_result_t result = BSP_FpgaInit();
    return result.ready;
}

bool BSP_FpgaAutoReconfigureEnabled(void)
{
#if FORGIX_FPGA_AUTO_RECONFIGURE
    return true;
#else
    return false;
#endif
}

bool BSP_FpgaIsReady(void)
{
    return fpga_ready;
}

bool BSP_FpgaCdone(void)
{
    return gpio_get(PIN_CDONE);
}

uint8_t BSP_FpgaPing(void)
{
    const uint8_t tx[] = {CMD_PING};
    return transaction(tx, 1, true);
}

uint8_t BSP_FpgaReadStatus(void)
{
    return BSP_FpgaReadRegister(REG_STATUS);
}

bool BSP_FpgaStatusPin(void)
{
    return gpio_get(PIN_STATUS);
}

void BSP_FpgaReset(void)
{
    const uint8_t tx[] = {CMD_RESET};
    transaction(tx, 1, false);
}

uint8_t BSP_FpgaReadRegister(const uint8_t address)
{
    const uint8_t tx[] = {CMD_READ, address};
    return transaction(tx, 2, true);
}

void BSP_FpgaWriteRegister(const uint8_t address, const uint8_t value)
{
    const uint8_t tx[] = {CMD_WRITE, address, value};
    transaction(tx, 3, false);
}
