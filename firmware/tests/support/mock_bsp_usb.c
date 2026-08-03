/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "mock_bsp_usb.h"




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


static bool _present;
static bool _connected;
static bsp_usb_health_t _health;
static uint32_t _serviceCount;




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


void MOCK_BSP_UsbReset( void )
{
    _present = true;
    _connected = true;
    _health = (bsp_usb_health_t) { 0 };
    _serviceCount = 0;
}


void MOCK_BSP_UsbSetPresent( const bool value )
{
    _present = value;
}


void MOCK_BSP_UsbSetConnected( const bool value )
{
    _connected = value;
}


void MOCK_BSP_UsbSetHealth( const bsp_usb_health_t value )
{
    _health = value;
}


uint32_t MOCK_BSP_UsbServiceCount( void )
{
    return _serviceCount;
}


bool BSP_UsbPresent( void )
{
    return _present;
}


bsp_usb_health_t BSP_UsbHealth( void )
{
    return _health;
}


bool BSP_UsbConnected( void )
{
    return _connected;
}


void BSP_UsbService( void )
{
    ++_serviceCount;
}
