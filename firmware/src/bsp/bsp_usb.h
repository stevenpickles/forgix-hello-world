#ifndef FORGIX_BSP_USB_H
#define FORGIX_BSP_USB_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_types.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


typedef struct bsp_usb_health_t_tag
{
    bool connected;           /* host asserted DTR on the CDC interface */
    bool suspended;           /* device stack reports USB bus suspend */
    uint32_t write_available; /* free bytes in the CDC transmit FIFO */
    uint32_t activity_count;  /* bumped on every completed CDC transfer */
    uint32_t frame_number;    /* host start-of-frame counter; frozen means no SOF */
} bsp_usb_health_t;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


/* True in images that link the real USB device stack, false in the stubbed
   USB-free image. The diagnostics layer uses it to choose between a serial boot
   report and an LED blink code. */
bool BSP_UsbPresent( void );

bsp_usb_health_t BSP_UsbHealth( void );

bool BSP_UsbConnected( void );

/* Services the device stack from the foreground loop when the firmware is built
   with FORGIX_FOREGROUND_USB_SERVICE; a no-op otherwise, because the SDK's
   background IRQ task already owns tud_task(). */
void BSP_UsbService( void );

#ifdef __cplusplus
}
#endif

#endif
