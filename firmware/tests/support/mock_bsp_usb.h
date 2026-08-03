#ifndef FORGIX_MOCK_BSP_USB_H
#define FORGIX_MOCK_BSP_USB_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_usb.h"

/* Defaults to a present, connected, idle-but-healthy host so console tests that
   do not care about USB keep their original behavior. */
void mock_bsp_usb_reset(void);
void mock_bsp_usb_set_present(bool present);
void mock_bsp_usb_set_connected(bool connected);
void mock_bsp_usb_set_health(bsp_usb_health_t health);
uint32_t mock_bsp_usb_service_count(void);

#endif
