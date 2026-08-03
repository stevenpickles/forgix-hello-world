/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_usb.h"




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/* Linked into the USB-free diagnostic image. It reports a permanently absent
   host so the shared application code takes its non-USB paths: no unsolicited
   status output, and an LED blink code instead of a serial boot report. */


/// <summary>
///     Always false here, which is what selects the LED blink code over the
///     serial boot report in the shared application layer.
/// </summary>
/// <returns>
///     False, unconditionally.
/// </returns>
bool BSP_UsbPresent( void )
{
    return false;
}

/// <summary>
///     All-zero health. Callers must gate on BSP_UsbPresent rather than reading
///     meaning into these fields, since zero here means absent rather than
///     unhealthy.
/// </summary>
/// <returns>
///     A zeroed report.
/// </returns>
bsp_usb_health_t BSP_UsbHealth( void )
{
    const bsp_usb_health_t health = { 0 };
    return health;
}

/// <summary>
///     Never connected, so the console layer's DTR gate keeps unsolicited
///     output off the wire in this image.
/// </summary>
/// <returns>
///     False, unconditionally.
/// </returns>
bool BSP_UsbConnected( void )
{
    return false;
}

/// <summary>
///     Empty by design. The foreground loop calls this unconditionally, so the
///     USB-free image needs the symbol to exist even though there is no device
///     stack behind it.
/// </summary>
void BSP_UsbService( void )
{
}
