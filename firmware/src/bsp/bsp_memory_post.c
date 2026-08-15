/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_memory.h"

#include "bsp_memory_internal.h"
#include "bsp_memory_verdict.h"
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

/* B5h, the 24-bit MR0 address, one byte spanning eight wait cycles, then one
   byte clocking out MR0. Both voltage variants permit this well above 25 MHz. */
#define MR0_READ_BYTES ( (uint32_t) 6u )
#define MR0_INDEX ( (uint32_t) 5u )

/* The final 64 bytes of the 2 MiB device are scratch at boot, before any
   consumer can own PSRAM content. Four-byte transactions keep the longest
   nine-byte fast read below the fitted part's 3 us extended-grade tCEM. */
#define SCRATCH_BASE ( (uint32_t) 0x1fffc0u )
#define SCRATCH_CHUNKS ( (uint32_t) 16u )
#define SCRATCH_CHUNK_BYTES ( (uint32_t) 4u )
#define SCRATCH_PASSES ( (uint32_t) 2u )
#define WRITE_OP_BYTES ( (uint32_t) 8u )
#define READ_OP_BYTES ( (uint32_t) 9u )
#define READ_DATA_INDEX ( (uint32_t) 5u )

#define OPERATION_COUNT ( 6u + ( SCRATCH_PASSES * 2u * SCRATCH_CHUNKS ) )

_Static_assert( SYS_CLK_HZ / POST_CLKDIV <= 33000000u,
                "Read-ID must stay at or under 33 MHz" );
_Static_assert( ( 8ull * READ_ID_BYTES * POST_CLKDIV * 1000000000ull ) / SYS_CLK_HZ < 3000ull,
                "Read-ID must hold chip select shorter than 3 us" );
_Static_assert( ( 8ull * READ_OP_BYTES * POST_CLKDIV * 1000000000ull ) / SYS_CLK_HZ < 3000ull,
                "scratch reads must hold chip select shorter than 3 us" );
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
static uint8_t _mr0Read[ MR0_READ_BYTES ] = {
    0xb5u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
};
static uint8_t _discard[ 1 ];
static uint8_t _idResponse[ READ_ID_BYTES ];
static uint8_t _mr0Response[ MR0_READ_BYTES ];
static uint8_t _writeTransmit[ SCRATCH_PASSES ][ SCRATCH_CHUNKS ][ WRITE_OP_BYTES ];
static uint8_t _readTransmit[ SCRATCH_CHUNKS ][ READ_OP_BYTES ];
static uint8_t _readResponse[ SCRATCH_PASSES ][ SCRATCH_CHUNKS ][ READ_OP_BYTES ];
static bsp_memory_cs_operation_t _operations[ OPERATION_COUNT ];
#endif




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


#if FORGIX_QSPI_PSRAM
static uint32_t _BuildOperationList( void );

static bool _ScratchVerified( uint32_t *ptr_failAddress );
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
    const uint32_t operationCount = _BuildOperationList();
    BSP_MemoryCs1OperationSequence( _operations, operationCount, POST_CLKDIV );

    _postReport.ran = true;
    _postReport.mfid = _idResponse[ MFID_INDEX ];
    _postReport.kgd = _idResponse[ KGD_INDEX ];
    _postReport.eid = _idResponse[ EID_INDEX ];
    _postReport.mr0 = _mr0Response[ MR0_INDEX ];
    _postReport.scratch_ok = _ScratchVerified( &_postReport.scratch_fail_address );
    _postReport.restored = BSP_MemoryPsramForceFromDatasheet();
    BSP_MemoryPsramRecordIdentity( _postReport.kgd, _postReport.eid );
    _postReport.result =
        BSP_MemoryVerdictPostClassify( _postReport.mfid, _postReport.kgd, _postReport.eid,
                                       _postReport.mr0, _postReport.scratch_ok,
                                       _postReport.restored );
#else
    _postReport.result = BSP_MEMORY_POST_SKIPPED;
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




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


#if FORGIX_QSPI_PSRAM
/// <summary>
///     Builds the single SRAM-resident operation list: reset, identity, MR0,
///     then two complete write-before-read scratch passes.
/// </summary>
/// <returns>
///     The fixed number of operations populated.
/// </returns>
static uint32_t _BuildOperationList( void )
{
    uint32_t operation = 0u;

    _operations[ operation++ ] = ( bsp_memory_cs_operation_t ){
        _resetEnable, NULL, 1u, true, POST_DESELECT_WAIT_CYCLES };
    _operations[ operation++ ] = ( bsp_memory_cs_operation_t ){
        _reset, NULL, 1u, true, POST_TRST_WAIT_CYCLES };
    _operations[ operation++ ] = ( bsp_memory_cs_operation_t ){
        _resetEnable, _discard, 1u, false, POST_DESELECT_WAIT_CYCLES };
    _operations[ operation++ ] = ( bsp_memory_cs_operation_t ){
        _reset, _discard, 1u, false, POST_TRST_WAIT_CYCLES };
    _operations[ operation++ ] = ( bsp_memory_cs_operation_t ){
        _readId, _idResponse, READ_ID_BYTES, false, POST_DESELECT_WAIT_CYCLES };
    _operations[ operation++ ] = ( bsp_memory_cs_operation_t ){
        _mr0Read, _mr0Response, MR0_READ_BYTES, false, POST_DESELECT_WAIT_CYCLES };

    for ( uint32_t pass = 0u; pass < SCRATCH_PASSES; ++pass )
    {
        for ( uint32_t chunk = 0u; chunk < SCRATCH_CHUNKS; ++chunk )
        {
            const uint32_t address = SCRATCH_BASE + chunk * SCRATCH_CHUNK_BYTES;
            uint8_t *const ptr_write = _writeTransmit[ pass ][ chunk ];
            ptr_write[ 0 ] = 0x02u;
            ptr_write[ 1 ] = (uint8_t) ( address >> 16u );
            ptr_write[ 2 ] = (uint8_t) ( address >> 8u );
            ptr_write[ 3 ] = (uint8_t) address;
            for ( uint32_t byte = 0u; byte < SCRATCH_CHUNK_BYTES; ++byte )
            {
                ptr_write[ 4u + byte ] =
                    BSP_MemoryVerdictScratchByte( address + byte, pass != 0u );
            }
            _operations[ operation++ ] = ( bsp_memory_cs_operation_t ){
                ptr_write, NULL, WRITE_OP_BYTES, false, POST_DESELECT_WAIT_CYCLES };
        }

        for ( uint32_t chunk = 0u; chunk < SCRATCH_CHUNKS; ++chunk )
        {
            const uint32_t address = SCRATCH_BASE + chunk * SCRATCH_CHUNK_BYTES;
            uint8_t *const ptr_read = _readTransmit[ chunk ];
            ptr_read[ 0 ] = 0x0bu;
            ptr_read[ 1 ] = (uint8_t) ( address >> 16u );
            ptr_read[ 2 ] = (uint8_t) ( address >> 8u );
            ptr_read[ 3 ] = (uint8_t) address;
            for ( uint32_t byte = 4u; byte < READ_OP_BYTES; ++byte )
            {
                ptr_read[ byte ] = 0u;
            }
            _operations[ operation++ ] = ( bsp_memory_cs_operation_t ){
                ptr_read, _readResponse[ pass ][ chunk ], READ_OP_BYTES, false,
                POST_DESELECT_WAIT_CYCLES };
        }
    }

    return operation;
}


/// <summary>
///     Verifies both retained scratch responses and records the first mismatched
///     device address while still judging the complete region.
/// </summary>
/// <returns>
///     True when all 128 observed bytes matched their write pass.
/// </returns>
static bool _ScratchVerified( uint32_t *ptr_failAddress )
{
    bool verified = true;

    *ptr_failAddress = 0u;
    for ( uint32_t pass = 0u; pass < SCRATCH_PASSES; ++pass )
    {
        for ( uint32_t chunk = 0u; chunk < SCRATCH_CHUNKS; ++chunk )
        {
            const uint32_t address = SCRATCH_BASE + chunk * SCRATCH_CHUNK_BYTES;
            for ( uint32_t byte = 0u; byte < SCRATCH_CHUNK_BYTES; ++byte )
            {
                const uint8_t expected =
                    BSP_MemoryVerdictScratchByte( address + byte, pass != 0u );
                const uint8_t actual = _readResponse[ pass ][ chunk ][ READ_DATA_INDEX + byte ];
                if ( verified && actual != expected )
                {
                    verified = false;
                    *ptr_failAddress = address + byte;
                }
            }
        }
    }
    return verified;
}
#endif
