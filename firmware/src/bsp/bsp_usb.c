#include "bsp_usb.h"

#include "hardware/structs/usb.h"
#include "pico/stdio_usb.h"
#include "tusb.h"

/* pico_stdio_usb does not implement these CDC callbacks, so the BSP can claim
   them to count completed transfers without disturbing SDK stdio behavior. */
static volatile uint32_t cdc_activity_count;

void tud_cdc_rx_cb( const uint8_t interface )
{
    (void) interface;
    ++cdc_activity_count;
}

void tud_cdc_tx_complete_cb( const uint8_t interface )
{
    (void) interface;
    ++cdc_activity_count;
}

bool BSP_UsbPresent( void )
{
    return true;
}

bsp_usb_health_t BSP_UsbHealth( void )
{
    const bsp_usb_health_t health = {
        .connected = stdio_usb_connected(),
        .suspended = tud_suspended(),
        .write_available = tud_cdc_write_available(),
        .activity_count = cdc_activity_count,
        .frame_number = usb_hw->sof_rd & USB_SOF_RD_BITS,
    };
    return health;
}

bool BSP_UsbConnected( void )
{
    return stdio_usb_connected();
}

void BSP_UsbService( void )
{
#if FORGIX_FOREGROUND_USB_SERVICE
    tud_task();
#endif
}
