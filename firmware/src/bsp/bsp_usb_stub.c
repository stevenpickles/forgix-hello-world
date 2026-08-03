#include "bsp_usb.h"

/* Linked into the USB-free diagnostic image. It reports a permanently absent
   host so the shared application code takes its non-USB paths: no unsolicited
   status output, and an LED blink code instead of a serial boot report. */

bool BSP_UsbPresent(void) {
    return false;
}

bsp_usb_health_t BSP_UsbHealth(void) {
    const bsp_usb_health_t health = {0};
    return health;
}

bool BSP_UsbConnected(void) {
    return false;
}

void BSP_UsbService(void) {
}
