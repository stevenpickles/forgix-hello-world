/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_button.h"

#include "bsp_fpga.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


/* Addresses in the FPGA register map that this module reads through
   BSP_FpgaReadRegister: REG_BUTTON holds the live pressed/released level and
   REG_BUTTON_COUNT holds the FPGA's running count of press edges. */
typedef enum bsp_button_register_tag
{
    REG_BUTTON = 0x20,
    REG_BUTTON_COUNT = 0x21,
} bsp_button_register;




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Reads the button level and the FPGA's edge count in one pass. The count
///     is maintained in the FPGA rather than by polling, so presses shorter than
///     the foreground loop period are still seen.
/// </summary>
/// <returns>
///     Live level, and presses counted since the FPGA was configured.
/// </returns>
bsp_button_state_t BSP_ButtonGetState( void )
{
    const bsp_button_state_t state = {
        .level = BSP_FpgaReadRegister( REG_BUTTON ),
        .count = BSP_FpgaReadRegister( REG_BUTTON_COUNT ),
    };
    return state;
}
