#include "mock_bsp_usb.h"

static bool present;
static bool connected;
static bsp_usb_health_t health;
static uint32_t service_count;

void mock_bsp_usb_reset(void) {
    present = true;
    connected = true;
    health = (bsp_usb_health_t){0};
    service_count = 0;
}

void mock_bsp_usb_set_present(const bool value) {
    present = value;
}

void mock_bsp_usb_set_connected(const bool value) {
    connected = value;
}

void mock_bsp_usb_set_health(const bsp_usb_health_t value) {
    health = value;
}

uint32_t mock_bsp_usb_service_count(void) {
    return service_count;
}

bool bsp_usb_present(void) {
    return present;
}

bsp_usb_health_t bsp_usb_health(void) {
    return health;
}

bool bsp_usb_connected(void) {
    return connected;
}

void bsp_usb_service(void) {
    ++service_count;
}
