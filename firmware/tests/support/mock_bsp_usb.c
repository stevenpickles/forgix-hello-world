#include "mock_bsp_usb.h"

static bool present;
static bool connected;
static bsp_usb_health_t health;
static uint32_t service_count;

void MOCK_BSP_UsbReset(void) {
    present = true;
    connected = true;
    health = (bsp_usb_health_t){0};
    service_count = 0;
}

void MOCK_BSP_UsbSetPresent(const bool value) {
    present = value;
}

void MOCK_BSP_UsbSetConnected(const bool value) {
    connected = value;
}

void MOCK_BSP_UsbSetHealth(const bsp_usb_health_t value) {
    health = value;
}

uint32_t MOCK_BSP_UsbServiceCount(void) {
    return service_count;
}

bool BSP_UsbPresent(void) {
    return present;
}

bsp_usb_health_t BSP_UsbHealth(void) {
    return health;
}

bool BSP_UsbConnected(void) {
    return connected;
}

void BSP_UsbService(void) {
    ++service_count;
}
