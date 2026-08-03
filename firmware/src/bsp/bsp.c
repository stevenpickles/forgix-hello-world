#include "bsp.h"

#include "hardware/gpio.h"
#include "hardware/structs/pads_bank0.h"

/* GPIO 0 is XIP_CS1n, the chip select for the secondary QSPI memory. That is the
   configuration Raspberry Pi documents in "Hardware design with RP2350" section
   3.2, and this board follows it: the device shares SCLK and SD0..SD3 with the
   boot flash, with GPIO 0 as the only thing separating them.

   The design guide is explicit that this arrangement needs a 10K pull-up on the
   chip select, "as the default state of GPIO0 is to be pulled low at power-up,
   which would cause our flash device to fail". This board has no such resistor.

   So the pin is not merely undriven, it is pulled *low* -- and the chip select
   is active low, which selects the secondary device from power-up. It then
   answers traffic meant for the boot flash and drives the shared data lines,
   corrupting XIP fetches. The core hangs mid-instruction, which is why no
   progress marker was ever recorded, and the watchdog resets into a bootrom
   that cannot read flash either, so nothing returns until power is cycled.

   Firmware cannot fix this properly: nothing runs before the first instruction,
   so the bootrom's own flash reads happen while the device is still selected.
   The real fix is the missing pull-up. What we can do is take the pin off its
   pull-down and hold it deasserted for everything after bring-up, which a two
   hour soak showed is enough to make the fault disappear in practice.

   The project does not use the secondary memory. To use it instead, select
   GPIO_FUNC_XIP_CS1 here and declare the device through flash_devinfo, and fit
   the pull-up first. */
#ifndef FORGIX_QSPI_CS1_GPIO
#define FORGIX_QSPI_CS1_GPIO 0
#endif

static void configure_qspi_cs1( void )
{
    /* RP2350-E14: the bootrom clears pad isolation for GPIO 0 rather than the
       configured chip select. Clear it explicitly so this does not depend on
       that erratum happening to name the right pin. */
    hw_clear_bits( &pads_bank0_hw->io[ FORGIX_QSPI_CS1_GPIO ], PADS_BANK0_GPIO0_ISO_BITS );

    /* The pull is what matters most: swapping the power-up pull-down for a
       pull-up is the only stand-in available for the 10K resistor the board
       cannot fit, and on an active-low chip select it is the difference between
       the device being selected at rest and deselected at rest. */
    gpio_set_pulls( FORGIX_QSPI_CS1_GPIO, true, false );

#if !FORGIX_QSPI_PSRAM
    /* Nothing is going to use the device, so hold it firmly deasserted. */
    gpio_init( FORGIX_QSPI_CS1_GPIO );
    /* Drive the output register before enabling the driver, so the pin never
       presents a low -- a momentary low is a chip select. */
    gpio_put( FORGIX_QSPI_CS1_GPIO, 1 );
    gpio_set_dir( FORGIX_QSPI_CS1_GPIO, GPIO_OUT );
#endif
    /* With FORGIX_QSPI_PSRAM the pin is left alone beyond the pull: runtime_init
       has already given it GPIO_FUNC_XIP_CS1 and the QMI drives it, idle high.
       Taking it back to SIO here would cut the DRAM off the bus. */
}

bsp_init_result_t BSP_Init( void )
{
    configure_qspi_cs1();
    BSP_ConsoleInit();
    return BSP_FpgaInit();
}
