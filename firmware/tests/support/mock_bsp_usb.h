#ifndef FORGIX_MOCK_BSP_USB_H
#define FORGIX_MOCK_BSP_USB_H

#include "bsp_types.h"
#include "bsp_usb.h"

/* Defaults to a present, connected, idle-but-healthy host so console tests that
   do not care about USB keep their original behavior. */
void MOCK_BSP_UsbReset(void);
void MOCK_BSP_UsbSetPresent(const bool present);
void MOCK_BSP_UsbSetConnected(const bool connected);
void MOCK_BSP_UsbSetHealth(const bsp_usb_health_t health);
uint32_t MOCK_BSP_UsbServiceCount(void);

#endif
