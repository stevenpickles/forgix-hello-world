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
typedef enum
{
    REG_BUTTON = 0x20,
    REG_BUTTON_COUNT = 0x21,
} bsp_button_register_t;




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


bsp_button_state_t BSP_ButtonGetState( void )
{
    const bsp_button_state_t state = {
        .level = BSP_FpgaReadRegister( REG_BUTTON ),
        .count = BSP_FpgaReadRegister( REG_BUTTON_COUNT ),
    };
    return state;
}
