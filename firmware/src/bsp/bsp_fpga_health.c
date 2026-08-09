/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_fpga_health.h"

#include "bsp_fpga.h"




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* Whether the FPGA is currently usable: true after a bring-up that ended with
   the expected design ID answering, false again the moment the runtime health
   check reports a failure. Distinct from the bring-up result itself, which is a
   fact about the past -- this latch is the only thing the foreground loop may
   consult without clocking the bus. */
static bool _ready;




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     The one rule that turns bring-up evidence into a readiness verdict: the
///     bitstream loaded and the design answered a ping with the expected ID. A
///     configured FPGA answering wrongly is a stale or corrupt design, not a bus
///     fault, and is exactly as unusable as one that never configured.
/// </summary>
/// <returns>
///     True when the evidence says the expected design is loaded and answering.
/// </returns>
bool BSP_FpgaHealthReadyVerdict( const bool configured, const uint8_t designId )
{
    return configured && ( designId == BSP_FPGA_DESIGN_ID );
}

/// <summary>
///     Stores the verdict. Bring-up calls this with whatever it concluded; the
///     runtime health check calls it with false when the FPGA stops answering,
///     and never with true -- a passing sample proves this second's probe worked,
///     not that a failed configuration healed itself.
/// </summary>
void BSP_FpgaHealthSetReady( const bool ready )
{
    _ready = ready;
}

/// <summary>
///     The latch, readable without touching hardware, which is what lets the
///     menu and the command gate ask every pass without clocking the bus.
/// </summary>
/// <returns>
///     True while the last bring-up succeeded and no runtime failure has been
///     reported since.
/// </returns>
bool BSP_FpgaHealthIsReady( void )
{
    return _ready;
}
