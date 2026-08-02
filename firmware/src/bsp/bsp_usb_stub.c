#include "bsp_usb.h"

/* Linked into the USB-free diagnostic image. It reports a permanently absent
   host so the shared application code takes its non-USB paths: no unsolicited
   status output, and an LED blink code instead of a serial boot report. */

bool bsp_usb_present(void) {
    return false;
}

bsp_usb_health_t bsp_usb_health(void) {
    bsp_usb_health_t health = {0};
    return health;
}

bool bsp_usb_connected(void) {
    return false;
}

void bsp_usb_service(void) {
}
