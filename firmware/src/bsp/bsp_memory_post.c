/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_memory.h"

#include "bsp_memory_internal.h"
#include "pico/stdlib.h"




/***************************************************************************************
**
** Compiler Define Directives
**
***************************************************************************************/


#if FORGIX_QSPI_PSRAM
/* clk_sys / 6 is 25 MHz: below the 33 MHz ceiling both voltage variants give
   SPI Read-ID, while a seven-byte transfer occupies 2.24 us and remains below
   the 3 us extended-grade tCEM of the fitted 3.3 V part. */
#define POST_CLKDIV ( (uint32_t) 6u )

/* Both datasheets require at least 50 ns from Reset to the next valid command.
   Ten microseconds is spent as an SRAM-resident cycle delay inside the one
   XIP-down window, so neither boot2 nor an interrupt can insert bus traffic. */
#define POST_TRST_WAIT_CYCLES ( (uint32_t) 1500u )

#define POST_DESELECT_WAIT_CYCLES ( (uint32_t) 15u )

#define READ_ID_BYTES ( (uint32_t) 7u )
#define MFID_INDEX ( (uint32_t) 4u )
#define KGD_INDEX ( (uint32_t) 5u )
#define EID_INDEX ( (uint32_t) 6u )

_Static_assert( SYS_CLK_HZ / POST_CLKDIV <= 33000000u,
                "Read-ID must stay at or under 33 MHz" );
_Static_assert( ( 8ull * READ_ID_BYTES * POST_CLKDIV * 1000000000ull ) / SYS_CLK_HZ < 3000ull,
                "Read-ID must hold chip select shorter than 3 us" );
#endif




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


static bsp_memory_post_report_t _postReport;

#if FORGIX_QSPI_PSRAM
/* Deliberately writable statics: .bss/.data are copied into SRAM. The failed
   feature branch made these const, placing them in flash and then dereferenced
   them after its own engine had disabled XIP. */
static uint8_t _resetEnable[ 1 ] = { 0x66u };
static uint8_t _reset[ 1 ] = { 0x99u };
static uint8_t _readId[ READ_ID_BYTES ] = {
    0x9fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
};
static uint8_t _discard[ 1 ];
static uint8_t _idResponse[ READ_ID_BYTES ];
static bsp_memory_cs_operation_t _operations[ 5 ];
#endif




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Performs the datasheet's boot-only reset and SPI Read-ID sequence before
///     USB exists. Quad reset first recovers a warm-reset device left in QPI;
///     serial reset then establishes the required SPI state. The reset pair and
///     Read-ID remain inside one XIP-down window with explicit tRST delays.
/// </summary>
/// <returns>
///     The raw identity and whether the normal QPI window was restored.
/// </returns>
bsp_memory_post_report_t BSP_MemoryPsramPost( void )
{
    _postReport = ( bsp_memory_post_report_t ){ 0 };

#if FORGIX_QSPI_PSRAM
    _operations[ 0 ] = ( bsp_memory_cs_operation_t ){
        _resetEnable, NULL, 1u, true, POST_DESELECT_WAIT_CYCLES };
    _operations[ 1 ] = ( bsp_memory_cs_operation_t ){
        _reset, NULL, 1u, true, POST_TRST_WAIT_CYCLES };
    _operations[ 2 ] = ( bsp_memory_cs_operation_t ){
        _resetEnable, _discard, 1u, false, POST_DESELECT_WAIT_CYCLES };
    _operations[ 3 ] = ( bsp_memory_cs_operation_t ){
        _reset, _discard, 1u, false, POST_TRST_WAIT_CYCLES };
    _operations[ 4 ] = ( bsp_memory_cs_operation_t ){
        _readId, _idResponse, READ_ID_BYTES, false, POST_DESELECT_WAIT_CYCLES };

    BSP_MemoryCs1OperationSequence( _operations,
                                    (uint32_t) ( sizeof _operations / sizeof _operations[ 0 ] ),
                                    POST_CLKDIV );

    _postReport.ran = true;
    _postReport.mfid = _idResponse[ MFID_INDEX ];
    _postReport.kgd = _idResponse[ KGD_INDEX ];
    _postReport.eid = _idResponse[ EID_INDEX ];
    _postReport.restored = BSP_MemoryPsramForceFromDatasheet();
    BSP_MemoryPsramRecordIdentity( _postReport.kgd, _postReport.eid );
#endif

    return _postReport;
}


/// <summary>
///     Returns the report captured before USB initialization without issuing a
///     command to the PSRAM. This is the only legal runtime identity path.
/// </summary>
/// <returns>
///     The boot capture, or a zeroed report when PSRAM support is disabled.
/// </returns>
bsp_memory_post_report_t BSP_MemoryPsramPostReport( void )
{
    return _postReport;
}
