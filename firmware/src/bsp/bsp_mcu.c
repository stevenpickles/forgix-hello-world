/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_mcu.h"

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/sysinfo.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/unique_id.h"

#include <string.h>




/***************************************************************************************
**
** Compiler Define Directives
**
***************************************************************************************/


/* SYSINFO CHIP_ID is a JEDEC JEP-106 identifier, not three packed fields at the
   obvious offsets. Bit 0 is the JEP-106 stop bit, so the manufacturer occupies
   bits 11:1 and has to be shifted down by one; reading it as bits 11:0 yields
   the manufacturer doubled with the stop bit in the low place -- 0x927 for the
   0x493 this part actually reports, which looks plausible enough to ship. */
#define CHIP_ID_MANUFACTURER_SHIFT ( (uint32_t) 1u )
#define CHIP_ID_MANUFACTURER_MASK ( (uint32_t) 0x000007ffu )
#define CHIP_ID_PART_SHIFT ( (uint32_t) 12u )
#define CHIP_ID_PART_MASK ( (uint32_t) 0x0000ffffu )
#define CHIP_ID_REVISION_SHIFT ( (uint32_t) 28u )
#define CHIP_ID_REVISION_MASK ( (uint32_t) 0x0000000fu )


/* Words the bootrom writes for SYS_INFO_CHIP_INFO: the echoed request flag,
   then package_id, device_id_lo and device_id_hi. */
#define CHIP_INFO_WORDS ( (uint32_t) 4u )


/* Argument pair for reset_usb_boot. No GPIO is nominated as an activity LED
   because the only LED on this board belongs to the FPGA, which loses its
   configuration the moment the MCU reboots; and no interface is disabled,
   because a user reaching for BOOTSEL wants whichever one their host supports. */
#define BOOTSEL_NO_ACTIVITY_LED ( (uint32_t) 0u )
#define BOOTSEL_DISABLE_NOTHING ( (uint32_t) 0u )




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* Captured once at init rather than read on demand. Two of these facts cost a
   flash command on the QSPI bus, and that bus is the one this board's missing
   chip-select pull-up makes fragile -- see bsp.c. Reading them once, early,
   behind the mitigation is a smaller exposure than reading them every time a
   user selects a menu item. */
static bsp_mcu_info_t _mcuInfo;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static void _ReadChipId( void );

static void _ReadChipInfo( void );

static void _ReadUniqueId( void );

static void _ReadMemorySizes( void );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Samples every fact the MCU reports about itself and caches it. Must run
///     after the QSPI chip-select fix, because reading the board identity issues
///     a flash command on the bus that fix protects.
/// </summary>
void BSP_McuInit( void )
{
    memset( &_mcuInfo, 0, sizeof _mcuInfo );

    _ReadChipId();
    _ReadChipInfo();
    _ReadUniqueId();
    _ReadMemorySizes();

    _mcuInfo.core_count = (uint8_t) NUM_CORES;
#ifdef __riscv
    _mcuInfo.architecture = BSP_MCU_ARCHITECTURE_RISCV;
#else
    _mcuInfo.architecture = BSP_MCU_ARCHITECTURE_ARM;
#endif
}


/// <summary>
///     The snapshot taken at init. Returned by value so a caller cannot hold a
///     pointer into BSP state across a reconfiguration.
/// </summary>
/// <returns>
///     Everything the MCU reported about itself at startup. All zero if
///     BSP_McuInit has not run.
/// </returns>
bsp_mcu_info_t BSP_McuInfo( void )
{
    return _mcuInfo;
}


/// <summary>
///     Restarts through the watchdog rather than jumping to the reset vector, so
///     the peripherals -- including the QSPI interface and the SPI block still
///     wired to the FPGA -- come back in their reset state rather than whatever
///     the running image left them in.
/// </summary>
void BSP_McuReboot( void )
{
    watchdog_reboot( 0, 0, 0 );
    while ( true )
    {
        /* watchdog_reboot returns immediately and the reset lands a moment
           later. Spinning here keeps the caller from running on with the board
           half torn down. */
    }
}


/// <summary>
///     Hands the board to the bootrom's USB loader so it can be reflashed
///     without unplugging it. Does not return: the bootrom takes the core.
/// </summary>
void BSP_McuRebootToBootsel( void )
{
    reset_usb_boot( BOOTSEL_NO_ACTIVITY_LED, BOOTSEL_DISABLE_NOTHING );
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/// <summary>
///     Splits the SYSINFO identity word. This register is on the always-on
///     domain and costs no bus traffic, which is why identity is taken from here
///     rather than from OTP.
/// </summary>
static void _ReadChipId( void )
{
    const uint32_t chipId = sysinfo_hw->chip_id;

    _mcuInfo.manufacturer =
        (uint16_t) ( ( chipId >> CHIP_ID_MANUFACTURER_SHIFT ) & CHIP_ID_MANUFACTURER_MASK );
    _mcuInfo.part = (uint16_t) ( ( chipId >> CHIP_ID_PART_SHIFT ) & CHIP_ID_PART_MASK );
    _mcuInfo.revision =
        (uint8_t) ( ( chipId >> CHIP_ID_REVISION_SHIFT ) & CHIP_ID_REVISION_MASK );
}


/// <summary>
///     Asks the bootrom for the package and device identifiers. The query is
///     allowed to fail: chip_info_valid then stays false so the caller reports
///     nothing rather than reporting zeroes as if they were real.
/// </summary>
static void _ReadChipInfo( void )
{
    uint32_t words[ CHIP_INFO_WORDS ] = { 0 };
    const int returned = rom_get_sys_info( words, CHIP_INFO_WORDS, SYS_INFO_CHIP_INFO );

    if ( ( returned != (int) CHIP_INFO_WORDS ) || ( words[ 0 ] != SYS_INFO_CHIP_INFO ) )
    {
        return;
    }

    _mcuInfo.package_id = words[ 1 ];
    _mcuInfo.device_id_low = words[ 2 ];
    _mcuInfo.device_id_high = words[ 3 ];
    _mcuInfo.chip_info_valid = true;
}


/// <summary>
///     Copies the board identity the SDK reads out of the flash die. This is the
///     one call here that puts traffic on the shared QSPI bus, and the reason
///     the whole module samples at init instead of on demand.
/// </summary>
static void _ReadUniqueId( void )
{
    pico_unique_board_id_t identity;

    pico_get_unique_board_id( &identity );
    memcpy( _mcuInfo.unique_id, identity.id,
            sizeof _mcuInfo.unique_id < sizeof identity.id ? sizeof _mcuInfo.unique_id
                                                           : sizeof identity.id );
}


/// <summary>
///     Records how much memory there is, and separately what OTP claims. Flash
///     comes from what the firmware was linked for: OTP FLASH_DEVINFO is wrong
///     on this part -- 0xC, the maximum enum, against a real 2 MByte die -- so
///     believing it would report eight times the flash that exists.
/// </summary>
static void _ReadMemorySizes( void )
{
    _mcuInfo.sram_bytes = (uint32_t) ( SRAM_END - SRAM_BASE );
    _mcuInfo.flash_bytes = (uint32_t) PICO_FLASH_SIZE_BYTES;
    _mcuInfo.otp_cs0_size_code = (uint8_t) flash_devinfo_get_cs_size( 0 );
    _mcuInfo.otp_cs1_size_code = (uint8_t) flash_devinfo_get_cs_size( 1 );
}
