#include "bsp.h"

#include "hardware/gpio.h"
#include "hardware/structs/pads_bank0.h"

/* GPIO 0 carries CE# of the QSPI device on QMI chip select 1 -- the
   APS1604M-SQR footprint, marked do-not-populate but fitted on this board. That
   device shares SCLK and SD0..SD3 with the boot flash; only chip select keeps
   them apart, and the board has no pull-up on the net.

   FLASH_DEVINFO reports CS1_SIZE = 0, so the bootrom does not know the device
   exists and never drives its enable. An undriven active-low enable floats, and
   RP2350-E9 puts an undriven input in a leaky state that settles around 2 V.
   When it drifts far enough to read as asserted, the device drives the shared
   data lines during a flash fetch. The corrupted read hangs the core mid-XIP,
   the watchdog resets, and the bootrom then cannot read flash either -- so
   nothing comes back until the power is cycled.

   Holding the line high from the earliest possible point gives the flash sole
   ownership of the bus. */
#ifndef FORGIX_QSPI_CS1_GPIO
#define FORGIX_QSPI_CS1_GPIO 0
#endif

static void deselect_qspi_cs1(void) {
    /* RP2350-E14: the bootrom clears pad isolation for GPIO 0 rather than the
       configured chip select. Clear it explicitly so this does not depend on
       that erratum happening to name the right pin. */
    hw_clear_bits(&pads_bank0_hw->io[FORGIX_QSPI_CS1_GPIO], PADS_BANK0_GPIO0_ISO_BITS);

    gpio_init(FORGIX_QSPI_CS1_GPIO);
    /* Drive the output register before enabling the driver, so the pin never
       presents a low -- a momentary low is a chip select. */
    gpio_put(FORGIX_QSPI_CS1_GPIO, 1);
    gpio_set_dir(FORGIX_QSPI_CS1_GPIO, GPIO_OUT);
    /* Belt and braces: the pad keeps the line high even if something later
       returns the pin to an input. */
    gpio_set_pulls(FORGIX_QSPI_CS1_GPIO, true, false);
}

bsp_init_result_t bsp_init(void) {
    deselect_qspi_cs1();
    bsp_console_init();
    return bsp_fpga_init();
}
