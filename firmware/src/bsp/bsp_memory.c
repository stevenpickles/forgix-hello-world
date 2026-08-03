/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_memory.h"

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#if FORGIX_QSPI_PSRAM
#include "hardware/psram.h"
#endif




/***************************************************************************************
**
** Compiler Define Directives
**
***************************************************************************************/


#ifndef FORGIX_QSPI_CS1_GPIO
#define FORGIX_QSPI_CS1_GPIO 0
#endif

/* QMI chip select 1 is mapped here, immediately above the 16 MB chip-select-0
   window. See the PSRAM region in the SDK linker script. This is the XIP
   address the chip-select-1 PSRAM appears at once it has been mapped. */
#define PSRAM_WINDOW_BASE ( (uint32_t) 0x11000000u )

/* AP Memory's known-good-die byte, at offset 5 of the Read-ID response: the
   value the datasheet says the fitted part reports. A mismatch here means an
   unexpected vendor answered, not that the memory itself is broken. */
#define EXPECTED_KGD ( (uint8_t) 0x5du )




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* Filled in by the psram_eid_to_size override below, which the SDK calls from
   runtime_init. Written before main runs, so plain statics are sufficient. */
static uint8_t _reportedKgd;
static uint8_t _reportedEid;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bool _FlashReadsCoherently( const uint32_t flashBytes );

#if FORGIX_QSPI_PSRAM
static bool _ForcePsramFromDatasheet( void );

static bool _PsramHoldsAPattern( const uint32_t sizeBytes );
#endif




/***************************************************************************************
**
** Interrupt Handler Overrides
**
***************************************************************************************/


#if FORGIX_QSPI_PSRAM
/* The SDK declares psram_eid_to_size weak so applications can support parts it
   does not know. Overriding it here is not about the mapping -- that is
   reproduced exactly -- but about the arguments: this is the one place the raw
   identity bytes exist, read from the device by the SDK's own detection during
   runtime_init, and otherwise discarded once the size has been derived.
   Capturing them here costs no additional bus transaction and cannot disturb the
   device, which is precisely what every attempt to re-read them later did. */
size_t psram_eid_to_size( const uint8_t kgd, const uint8_t eid )
{
    _reportedKgd = kgd;
    _reportedEid = eid;

    if ( kgd != EXPECTED_KGD )
    {
        return 0;
    }

    /* Density lives in the top three bits of the EID, and the mapping is the
       SDK's, kept identical so overriding the hook changes nothing but
       observability. */
    uint32_t psramSize = 1024u * 1024u;
    const uint8_t sizeId = eid >> 5;
    if ( sizeId == 4u )
    {
        psramSize *= 16u;
    }
    else if ( eid == 0x26u || sizeId == 2u || sizeId == 3u )
    {
        psramSize *= 8u;
    }
    else if ( sizeId == 1u )
    {
        psramSize *= 4u;
    }
    else
    {
        psramSize *= 2u;
    }
    return (size_t) psramSize;
}
#endif




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


bsp_memory_report_t BSP_MemoryCheck( void )
{
    bsp_memory_report_t report = { 0 };

    report.psram_kgd = _reportedKgd;
    report.psram_eid = _reportedEid;
    report.flash_bytes = PICO_FLASH_SIZE_BYTES;
    report.flash_ok = _FlashReadsCoherently( report.flash_bytes );

#if FORGIX_QSPI_PSRAM
    if ( psram_is_available() )
    {
        report.psram_bytes = (uint32_t) psram_get_size();
        report.psram_ok = _PsramHoldsAPattern( report.psram_bytes );
    }
    else if ( _ForcePsramFromDatasheet() )
    {
        /* Auto-detection only compares the identity byte. This device answers
           Read-ID selectively and correctly, it just does not report AP Memory's
           vendor, so ask whether it works as memory rather than whether it says
           the right name. */
        report.psram_forced = true;
        report.psram_bytes = (uint32_t) psram_get_size();
        report.psram_ok = _PsramHoldsAPattern( report.psram_bytes );
    }
#endif

    return report;
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/* The boot flash is proven readable by the fact that this code is executing from
   it, so the useful check is that it still reads back coherently: a stack
   pointer in SRAM and a reset vector inside the flash window. Bus contention on
   the shared QSPI lines corrupts reads rather than stopping them, so a garbled
   vector table is exactly what a CS1 problem looks like from here. */
static bool _FlashReadsCoherently( const uint32_t flashBytes )
{
    const uint32_t *ptr_vectors = (const uint32_t *) XIP_BASE;
    const uint32_t stackPointer = ptr_vectors[ 0 ];
    const uint32_t resetVector = ptr_vectors[ 1 ];

    return stackPointer > SRAM_BASE && stackPointer <= SRAM_END && resetVector >= XIP_BASE &&
           resetVector < XIP_BASE + flashBytes;
}

#if FORGIX_QSPI_PSRAM
/* Configures chip select 1 from the datasheet instead of from what the device
   claims to be, then brings it up.

   APS1604M-3SQR at 3.3 V: 2 MByte, 84 MHz for linear-512 burst which is the
   conservative ceiling, tCEM 3 us at 105 C bounding how long chip select may
   stay asserted, and a deselect gap with margin over the specified minimum.

   psram_reinitialize is documented as unsafe against concurrent XIP, so it runs
   with interrupts off -- handlers live in flash. */
static bool _ForcePsramFromDatasheet( void )
{
    flash_devinfo_set_cs_gpio( 1, FORGIX_QSPI_CS1_GPIO );
    flash_devinfo_set_cs_size( 1, FLASH_DEVINFO_SIZE_2M );

    if ( psram_configure_params( 84u * 1000u * 1000u, 3000u, 50u ) != PICO_OK )
    {
        return false;
    }

    const uint32_t interrupts = save_and_disable_interrupts();
    /* int is the SDK's own return type for psram_reinitialize; kept as-is
       since that is the honest type at this boundary. */
    const int result = psram_reinitialize();
    restore_interrupts( interrupts );

    return result == PICO_OK && psram_get_size() > 0u;
}

/* Writes every pattern before reading any of them back. Checking each write
   immediately would pass against a bus that merely echoes the last value, and
   would not catch address aliasing from a device smaller than it reports. */
static bool _PsramHoldsAPattern( const uint32_t sizeBytes )
{
    if ( sizeBytes < (uint32_t) sizeof( uint32_t ) )
    {
        return false;
    }

    volatile uint32_t *const ptr_window = (volatile uint32_t *) PSRAM_WINDOW_BASE;
    const uint32_t words = sizeBytes / (uint32_t) sizeof( uint32_t );
    const uint32_t indices[] = { 0, words / 2u, words - 1u };
    const uint32_t patterns[] = { 0xa5a5a5a5u, 0x5a5a5a5au, 0xdeadbeefu };
    const uint32_t count = (uint32_t) ( sizeof indices / sizeof indices[ 0 ] );

    for ( uint32_t index = 0; index < count; ++index )
    {
        ptr_window[ indices[ index ] ] = patterns[ index ];
    }
    for ( uint32_t index = 0; index < count; ++index )
    {
        if ( ptr_window[ indices[ index ] ] != patterns[ index ] )
        {
            return false;
        }
    }
    return true;
}
#endif
