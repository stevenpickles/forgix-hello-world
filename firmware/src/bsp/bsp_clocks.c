/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_clocks.h"

#include "hardware/clocks.h"
#include "hardware/regs/clocks.h"




/***************************************************************************************
**
** Compiler Define Directives
**
***************************************************************************************/


#define HZ_PER_KHZ ( (uint32_t) 1000u )




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Reports every clock domain twice: what the SDK configured, and what the
///     frequency counter actually measures. The two disagree exactly when
///     something is wrong -- a PLL that failed to lock still reports its target
///     through clock_get_hz, because that value is bookkeeping the SDK wrote
///     down when it asked, not a reading.
/// </summary>
/// <returns>
///     Configured and measured frequencies in Hz. Measured values are counted
///     against clk_ref and so are ultimately referred to the crystal.
/// </returns>
bsp_clocks_report_t BSP_ClocksReport( void )
{
    const bsp_clocks_report_t report = {
        .sys_hz = clock_get_hz( clk_sys ),
        .usb_hz = clock_get_hz( clk_usb ),
        .ref_hz = clock_get_hz( clk_ref ),
        .peri_hz = clock_get_hz( clk_peri ),
        .adc_hz = clock_get_hz( clk_adc ),
        .measured_sys_hz = frequency_count_khz( CLOCKS_FC0_SRC_VALUE_CLK_SYS ) * HZ_PER_KHZ,
        .measured_usb_hz = frequency_count_khz( CLOCKS_FC0_SRC_VALUE_CLK_USB ) * HZ_PER_KHZ,
    };
    return report;
}
