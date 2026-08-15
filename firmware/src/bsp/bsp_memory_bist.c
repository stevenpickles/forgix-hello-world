/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_memory_bist.h"

#include "bsp_memory_bist_logic.h"
#include "hardware/regs/addressmap.h"

#if FORGIX_QSPI_PSRAM
#include "hardware/psram.h"
#endif




/***************************************************************************************
**
** Compiler Define Directives
**
***************************************************************************************/


/* The CS1 window through the no-allocate alias. Runtime BIST deliberately uses
   the controller's normal mapped-QPI path with interrupts and flash XIP left
   alone; only the boot POST is allowed to own QMI direct mode. */
#define PSRAM_WINDOW_BASE ( (uint32_t) 0x11000000u )
#define PSRAM_NOCACHE_BASE ( PSRAM_WINDOW_BASE + ( XIP_NOCACHE_NOALLOC_BASE - XIP_BASE ) )




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


#if FORGIX_QSPI_PSRAM
static bsp_memory_bist_context_t _bistContext;
static bool _bistFinished;
static bool _bistRestored;
static bsp_memory_bist_result _bistResult = BSP_MEMORY_BIST_PASS;

/* One foreground slice retained in SRAM for detailed comparison and the
   whole-slice stuck-bus check. It is not test memory: the PSRAM under test is
   reached only through the volatile no-cache window below. */
static uint8_t _bistSliceData[ BSP_MEMORY_BIST_SLICE_CHUNKS * BSP_MEMORY_BIST_CHUNK_BYTES ];
#endif




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


#if FORGIX_QSPI_PSRAM
static bool _WindowAvailable( void );

static void _RunMappedSlice( const bsp_memory_bist_slice_t *ptr_slice );

static void _FinishRun( void );
#endif




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Resets the test to pair zero's write sweep. The first Step performs the
///     first mapped access, keeping start cheap and abortable.
/// </summary>
void BSP_MemoryBistStart( void )
{
#if FORGIX_QSPI_PSRAM
    _bistContext = ( bsp_memory_bist_context_t ){ 0 };
    _bistFinished = false;
    _bistRestored = false;
    _bistResult = BSP_MEMORY_BIST_PASS;
#endif
}


/// <summary>
///     Runs one 4 KiB slice through the ordinary uncached QPI mapping. Flash XIP,
///     interrupts and the controller configuration remain untouched throughout;
///     this is deliberately a different mechanism from the boot-only POST.
/// </summary>
/// <returns>
///     Progress, fault records and the final verdict when the run has ended.
/// </returns>
bsp_memory_bist_status_t BSP_MemoryBistStep( void )
{
    bsp_memory_bist_status_t status = { 0 };

#if FORGIX_QSPI_PSRAM
    if ( !_bistFinished )
    {
        if ( !_WindowAvailable() )
        {
            _bistContext.dead_bus = true;
            _FinishRun();
        }
        else
        {
            const bsp_memory_bist_slice_t slice = BSP_MemoryBistNextSlice( &_bistContext );
            if ( !slice.done )
            {
                _RunMappedSlice( &slice );
                BSP_MemoryBistAdvance( &_bistContext, slice.chunk_count );
                status.pair = slice.pair;
                status.verifying = slice.verifying;
                status.sweep_boundary = ( _bistContext.chunks_done == 0u );
            }

            if ( _bistContext.dead_bus || BSP_MemoryBistNextSlice( &_bistContext ).done )
            {
                _FinishRun();
            }
        }
    }

    status.done = _bistFinished;
    status.result = _bistResult;
    status.restored = _bistRestored;
    status.bytes_done = BSP_MemoryBistProgressBytes( &_bistContext );
    status.error_total = _bistContext.log.total;
    status.log = _bistContext.log;
#else
    status.done = true;
    status.result = BSP_MEMORY_BIST_SKIPPED;
#endif

    return status;
}


/// <summary>
///     Stops between mapped slices. No bus-mode restoration is necessary because
///     the runtime test never leaves QPI or changes QMI; the return simply says
///     whether the expected 2 MiB window is still advertised.
/// </summary>
/// <returns>
///     True when the ordinary mapped window remains available.
/// </returns>
bool BSP_MemoryBistAbort( void )
{
#if FORGIX_QSPI_PSRAM
    _bistFinished = true;
    _bistRestored = _WindowAvailable();
    return _bistRestored;
#else
    return true;
#endif
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


#if FORGIX_QSPI_PSRAM
/// <summary>
///     Requires the exact device span the pattern planner addresses. A contained
///     watchdog recovery advertises zero bytes and therefore fails here before
///     the first volatile access.
/// </summary>
/// <returns>
///     True only for the complete 2 MiB mapped window.
/// </returns>
static bool _WindowAvailable( void )
{
    return (uint32_t) psram_get_size() == (uint32_t) BSP_MEMORY_BIST_DEVICE_BYTES;
}


/// <summary>
///     Writes or reads one planned slice at its alternating address order. A
///     verify stores the observations in SRAM first, then judges each chunk and
///     the slice-wide stuck-bus signature after the volatile bus reads finish.
/// </summary>
static void _RunMappedSlice( const bsp_memory_bist_slice_t *ptr_slice )
{
    for ( uint32_t chunk = 0u; chunk < ptr_slice->chunk_count; ++chunk )
    {
        const uint32_t address = BSP_MemoryBistChunkAddress( ptr_slice, chunk );
        volatile uint8_t *const ptr_memory = (volatile uint8_t *) ( PSRAM_NOCACHE_BASE + address );
        uint8_t *const ptr_data = &_bistSliceData[ chunk * (uint32_t) BSP_MEMORY_BIST_CHUNK_BYTES ];

        if ( !ptr_slice->verifying )
        {
            BSP_MemoryBistFillChunk( ptr_slice->pair, address, ptr_data,
                                     (uint32_t) BSP_MEMORY_BIST_CHUNK_BYTES );
            for ( uint32_t byte = 0u; byte < (uint32_t) BSP_MEMORY_BIST_CHUNK_BYTES; ++byte )
            {
                ptr_memory[ byte ] = ptr_data[ byte ];
            }
        }
        else
        {
            for ( uint32_t byte = 0u; byte < (uint32_t) BSP_MEMORY_BIST_CHUNK_BYTES; ++byte )
            {
                ptr_data[ byte ] = ptr_memory[ byte ];
            }
            BSP_MemoryBistVerifyChunk( &_bistContext, address, ptr_data,
                                       (uint32_t) BSP_MEMORY_BIST_CHUNK_BYTES );
        }
    }

    if ( ptr_slice->verifying )
    {
        BSP_MemoryBistNoteSliceObserved( &_bistContext, _bistSliceData,
                                         ptr_slice->chunk_count *
                                             (uint32_t) BSP_MEMORY_BIST_CHUNK_BYTES );
    }
}


/// <summary>
///     Latches a final verdict. Since mapped BIST never changed controller mode,
///     restoration is the same size check that guarded every slice.
/// </summary>
static void _FinishRun( void )
{
    _bistRestored = _WindowAvailable();
    _bistResult = BSP_MemoryBistClassify( &_bistContext, _bistRestored );
    _bistFinished = true;
}
#endif
