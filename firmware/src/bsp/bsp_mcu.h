#ifndef FORGIX_BSP_MCU_H
#define FORGIX_BSP_MCU_H

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
** Compiler Define Directives
**
***************************************************************************************/


/* Width of the board identity cached at init. Eight bytes is what the flash
   unique-ID command returns, and it is fixed here rather than probed because the
   report struct embeds the array by value. */
#define BSP_MCU_UNIQUE_ID_BYTES ( (uint32_t) 8u )


/* SYSINFO CHIP_ID fields the identity check expects on this board. A part number
   that is not RP2350 means the image is running on something it was not built
   for; the manufacturer field is Raspberry Pi's JEDEC continuation code. */
#define BSP_MCU_MANUFACTURER_RASPBERRY_PI ( (uint16_t) 0x0493u )


#define BSP_MCU_PART_RP2350 ( (uint16_t) 0x0004u )




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


/* Which instruction set this image was built for. RP2350 carries an Arm
   Cortex-M33 pair and a Hazard3 RISC-V pair on the same die and boots whichever
   the image targets, so this reports what is executing rather than what the
   silicon contains. */
typedef enum bsp_mcu_architecture_tag
{
    BSP_MCU_ARCHITECTURE_ARM,
    BSP_MCU_ARCHITECTURE_RISCV,
} bsp_mcu_architecture;


typedef struct bsp_mcu_info_t_tag
{
    uint16_t manufacturer; /* SYSINFO CHIP_ID manufacturer field */
    uint16_t part;         /* SYSINFO CHIP_ID part field */
    uint8_t revision;      /* silicon revision, 0 for A0 upwards */
    uint32_t package_id;
    uint32_t device_id_low;
    uint32_t device_id_high;
    /* True when the bootrom answered the chip-info query. The package and device
       identifiers are meaningless if it did not, so they must never be reported
       as facts on their own. */
    bool chip_info_valid;
    uint8_t unique_id[ BSP_MCU_UNIQUE_ID_BYTES ];
    uint32_t sram_bytes;
    uint32_t flash_bytes; /* what the firmware was linked for, not what OTP claims */
    /* Chip-select size codes as OTP FLASH_DEVINFO reports them. Reported raw and
       never acted on: this part answers 0xC for chip select 0, the maximum enum,
       which is a permissive unprogrammed default rather than the true 2 MByte,
       and 0 for chip select 1 even though a 2 MByte device is fitted there. */
    uint8_t otp_cs0_size_code;
    uint8_t otp_cs1_size_code;
    uint8_t core_count;
    bsp_mcu_architecture architecture;
} bsp_mcu_info_t;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


void BSP_McuInit( void );

bsp_mcu_info_t BSP_McuInfo( void );

void BSP_McuReboot( void );

void BSP_McuRebootToBootsel( void );

#ifdef __cplusplus
}
#endif

#endif
