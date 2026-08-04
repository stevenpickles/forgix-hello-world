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


/// <summary>
///     Restores the default of a present, connected host with zeroed health, so
///     console tests that do not care about USB behave as though it is simply
///     working.
/// </summary>
void MOCK_BSP_UsbReset( void )
{
    _present = true;
    _connected = true;
    _health = ( bsp_usb_health_t ){ 0 };
    _serviceCount = 0;
}


/// <summary>
///     Chooses which image the code under test believes it is running in, which is
///     what selects the serial boot report or the LED blink code.
/// </summary>
void MOCK_BSP_UsbSetPresent( const bool value )
{
    _present = value;
}


/// <summary>
///     Drives the DTR gate that gags unsolicited output, so a test can prove the
///     firmware stays quiet into a port nobody has opened.
/// </summary>
void MOCK_BSP_UsbSetConnected( const bool value )
{
    _connected = value;
}


/// <summary>
///     Installs a health snapshot wholesale, letting a test pose combinations the
///     real stack would take a live host to produce, such as a frozen frame
///     number alongside an otherwise healthy link.
/// </summary>
void MOCK_BSP_UsbSetHealth( const bsp_usb_health_t value )
{
    _health = value;
}


/// <summary>
///     How many times the foreground loop pumped the stack. The number matters
///     because servicing from the loop is conditional on a build option.
/// </summary>
/// <returns>
///     Calls to BSP_UsbService since the last reset.
/// </returns>
uint32_t MOCK_BSP_UsbServiceCount( void )
{
    return _serviceCount;
}


/// <summary>
///     Whatever the test last chose.
/// </summary>
/// <returns>
///     The staged presence flag.
/// </returns>
bool BSP_UsbPresent( void )
{
    return _present;
}


/// <summary>
///     Returns the staged snapshot unchanged; nothing here derives fields from one
///     another, so a test can stage a deliberately inconsistent link.
/// </summary>
/// <returns>
///     The staged health snapshot.
/// </returns>
bsp_usb_health_t BSP_UsbHealth( void )
{
    return _health;
}


/// <summary>
///     Whatever the test last chose, independent of the presence flag so absent
///     but connected can be posed if a test wants it.
/// </summary>
/// <returns>
///     The staged connection flag.
/// </returns>
bool BSP_UsbConnected( void )
{
    return _connected;
}


/// <summary>
///     Counts the call and does nothing else.
/// </summary>
void BSP_UsbService( void )
{
    ++_serviceCount;
}
