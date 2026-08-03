/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_usb.h"

#include "hardware/structs/usb.h"
#include "pico/stdio_usb.h"
#include "tusb.h"




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* Completed CDC transfers since boot. Bumped from interrupt context by the
   callbacks below, so the reader gets a snapshot rather than a stable count. */
static volatile uint32_t _cdcActivityCount;




/***************************************************************************************
**
** Interrupt Handler Overrides
**
***************************************************************************************/


/* pico_stdio_usb does not implement these CDC callbacks, so the BSP can claim
   them to count completed transfers without disturbing SDK stdio behavior. */

/// <summary>
///     Counts an inbound CDC transfer. The count is the only evidence that the
///     link is moving data rather than merely enumerated, which is what the
///     health report needs to distinguish a live host from a stalled one.
/// </summary>
void tud_cdc_rx_cb( const uint8_t interface )
{
    (void) interface;
    ++_cdcActivityCount;
}

/// <summary>
///     Counts a completed outbound CDC transfer, into the same counter as the
///     receive callback. Direction is not distinguished because the question
///     being asked is whether anything is moving at all.
/// </summary>
void tud_cdc_tx_complete_cb( const uint8_t interface )
{
    (void) interface;
    ++_cdcActivityCount;
}




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     True in this image because it links the real device stack. The stubbed
///     image answers false from the same prototype, which is how one
///     application binary serves both builds.
/// </summary>
/// <returns>
///     True, unconditionally.
/// </returns>
bool BSP_UsbPresent( void )
{
    return true;
}

/// <summary>
///     Samples the device stack in one pass so the fields describe the same
///     instant. The frame number matters most: a host that has stopped issuing
///     start-of-frame leaves it frozen while every other field still looks
///     healthy.
/// </summary>
/// <returns>
///     A snapshot of the link, valid only for the instant it was taken.
/// </returns>
bsp_usb_health_t BSP_UsbHealth( void )
{
    const bsp_usb_health_t health = {
        .connected = stdio_usb_connected(),
        .suspended = tud_suspended(),
        .write_available = tud_cdc_write_available(),
        .activity_count = _cdcActivityCount,
        .frame_number = usb_hw->sof_rd & USB_SOF_RD_BITS,
    };
    return health;
}

/// <summary>
///     Whether the host has asserted DTR, meaning something is actually reading
///     the port. Unsolicited output is gated on this: writing into a port no
///     host has opened is the firmware's one unbounded self-inflicted stall.
/// </summary>
/// <returns>
///     True while a host holds the CDC interface open.
/// </returns>
bool BSP_UsbConnected( void )
{
    return stdio_usb_connected();
}

/// <summary>
///     Pumps the device stack from the foreground loop, but only in images built
///     with FORGIX_FOREGROUND_USB_SERVICE. Otherwise a no-op, because the SDK's
///     background IRQ task already owns tud_task and calling it from both would
///     re-enter the stack.
/// </summary>
void BSP_UsbService( void )
{
#if FORGIX_FOREGROUND_USB_SERVICE
    tud_task();
#endif
}
