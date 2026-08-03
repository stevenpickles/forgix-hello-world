/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_led.h"

#include "bsp_fpga.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


/* Addresses in the FPGA register map this module writes and reads back: the
   three colour channels, the global brightness, and the enable bit that
   latches them into the physical LED. */
typedef enum bsp_led_register_tag
{
    REG_LED_R = 0x10,
    REG_LED_G = 0x11,
    REG_LED_B = 0x12,
    REG_LED_GLOBAL = 0x13,
    REG_LED_ENABLE = 0x14,
} bsp_led_register;




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Writes the three channels and the brightness, then sets the enable bit
///     last so the LED changes to the new colour in one step rather than
///     sweeping through the intermediate ones.
/// </summary>
void BSP_LedSet( const uint8_t red, const uint8_t green, const uint8_t blue,
                 const uint8_t brightness )
{
    BSP_FpgaWriteRegister( REG_LED_R, red );
    BSP_FpgaWriteRegister( REG_LED_G, green );
    BSP_FpgaWriteRegister( REG_LED_B, blue );
    BSP_FpgaWriteRegister( REG_LED_GLOBAL, brightness );
    BSP_FpgaWriteRegister( REG_LED_ENABLE, 1 );
}

/// <summary>
///     Clears the enable bit only. The colour registers keep their values, so a
///     later BSP_LedSet with the same arguments restores exactly what was
///     showing, and BSP_LedGet can still report what was last asked for.
/// </summary>
void BSP_LedOff( void )
{
    BSP_FpgaWriteRegister( REG_LED_ENABLE, 0 );
}

/// <summary>
///     Reads the LED registers back over the bus rather than returning a cached
///     copy. That is the point: the diagnostics layer compares this against what
///     it wrote, so a stuck or contended bus shows up as a mismatch.
/// </summary>
/// <returns>
///     What the FPGA currently holds in its LED registers.
/// </returns>
bsp_led_state_t BSP_LedGet( void )
{
    const bsp_led_state_t state = {
        .red = BSP_FpgaReadRegister( REG_LED_R ),
        .green = BSP_FpgaReadRegister( REG_LED_G ),
        .blue = BSP_FpgaReadRegister( REG_LED_B ),
        .brightness = BSP_FpgaReadRegister( REG_LED_GLOBAL ),
        .enabled = ( BSP_FpgaReadRegister( REG_LED_ENABLE ) & 1u ) != 0,
    };
    return state;
}
