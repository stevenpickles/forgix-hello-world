/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_memory_bist_logic.h"




/***************************************************************************************
**
** Compiler Define Directives
**
***************************************************************************************/


/* The address-derived scramble for pairs 0 and 1: every aligned 32-bit word
   holds its own address xor this constant (pair 0) or the complement of that
   (pair 1), little endian. Address-derived so that no two words in the device
   agree, which is what makes aliasing visible: a write that lands at the
   wrong physical row leaves the wrong address's pattern there, and the verify
   sweep -- which runs only after the whole device is written -- reads a value
   that names the address the data actually came from. */
#define ADDRESS_PATTERN_SEED ( (uint32_t) 0xa5a55a5au )

/* Address bits that can alias: the device decodes A[20:0]. */
#define ADDRESS_BITS ( (uint32_t) 21u )

#define BYTE_BITS ( (uint32_t) 8u )
#define WORD_ALIGN_MASK ( (uint32_t) 0x3u )




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* One row per pair, in run order: the two address-derived sweeps, the fixed
   bytes, a walking one, a walking zero, and the final clear that leaves the
   device deterministically zeroed. Ascending and descending traversal
   alternate by pair parity -- pair 1's descending order is part of its
   definition, and the alternation continues through the fixed patterns so
   both directions of address strobing get exercised across the run. */
static const uint8_t _fixedPattern[ BSP_MEMORY_BIST_PAIRS ] = {
    0x00u, 0x00u, /* pairs 0 and 1 derive from the address; entries unused */
    0x00u, 0xffu, 0xaau, 0x55u, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x20u, 0x40u,
    0x80u, 0xfeu, 0xfdu, 0xfbu, 0xf7u, 0xefu, 0xdfu, 0xbfu, 0x7fu, 0x00u,
};

static const char *const _pairLabel[ BSP_MEMORY_BIST_PAIRS ] = {
    "addr", "~addr", "00", "ff", "aa", "55", "01", "02", "04", "08", "10",    "20",
    "40",   "80",    "fe", "fd", "fb", "f7", "ef", "df", "bf", "7f", "clear",
};




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Derives the pattern byte for one absolute address under one pair. Pairs
///     0 and 1 extract their byte lane from the containing word's
///     address-derived value; every other pair is its fixed byte.
/// </summary>
/// <returns>
///     The byte the write sweep stores and the verify sweep demands there.
/// </returns>
uint8_t BSP_MemoryBistExpectedByte( const uint32_t pair, const uint32_t address )
{
    if ( pair <= 1u )
    {
        const uint32_t aligned = address & ~WORD_ALIGN_MASK;
        uint32_t word = aligned ^ ADDRESS_PATTERN_SEED;
        if ( pair == 1u )
        {
            word = ~word;
        }
        return (uint8_t) ( word >> ( BYTE_BITS * ( address & WORD_ALIGN_MASK ) ) );
    }
    return _fixedPattern[ pair ];
}


/// <summary>
///     Names the pair for a progress line.
/// </summary>
/// <returns>
///     A short static label: "addr", "~addr", a hex byte, or "clear".
/// </returns>
const char *BSP_MemoryBistPairLabel( const uint32_t pair )
{
    return _pairLabel[ pair ];
}


/// <summary>
///     Fills a buffer with the pattern bytes for one chunk, byte by byte from
///     the same derivation the verify uses, so fill and verify cannot drift.
/// </summary>
void BSP_MemoryBistFillChunk( const uint32_t pair, const uint32_t address, uint8_t *ptr_data,
                              const uint32_t count )
{
    for ( uint32_t byte = 0u; byte < count; ++byte )
    {
        ptr_data[ byte ] = BSP_MemoryBistExpectedByte( pair, address + byte );
    }
}


/// <summary>
///     Describes the next slice of work from where the context stands: which
///     pair, which sweep, which chunk ordinals, and in which direction --
///     without changing anything, so an aborted slice was simply never run.
/// </summary>
/// <returns>
///     The slice, with done set once every pair has finished.
/// </returns>
bsp_memory_bist_slice_t BSP_MemoryBistNextSlice( const bsp_memory_bist_context_t *ptr_context )
{
    bsp_memory_bist_slice_t slice = { 0 };

    if ( ptr_context->pair >= (uint32_t) BSP_MEMORY_BIST_PAIRS )
    {
        slice.done = true;
        return slice;
    }

    slice.pair = ptr_context->pair;
    slice.verifying = ptr_context->verifying;
    slice.first_chunk = ptr_context->chunks_done;
    const uint32_t remaining = (uint32_t) BSP_MEMORY_BIST_SWEEP_CHUNKS - ptr_context->chunks_done;
    slice.chunk_count = ( remaining < (uint32_t) BSP_MEMORY_BIST_SLICE_CHUNKS )
                            ? remaining
                            : (uint32_t) BSP_MEMORY_BIST_SLICE_CHUNKS;
    slice.descending = ( ptr_context->pair & 1u ) != 0u;
    return slice;
}


/// <summary>
///     Maps one chunk of a slice to its bus address. Descending pairs issue
///     their chunks from the top of the device downwards; the bytes inside a
///     chunk always ascend, because one bus burst is inherently ascending.
/// </summary>
/// <returns>
///     The absolute device address of the chunk's first byte.
/// </returns>
uint32_t BSP_MemoryBistChunkAddress( const bsp_memory_bist_slice_t *ptr_slice,
                                     const uint32_t index )
{
    uint32_t ordinal = ptr_slice->first_chunk + index;
    if ( ptr_slice->descending )
    {
        ordinal = ( (uint32_t) BSP_MEMORY_BIST_SWEEP_CHUNKS - 1u ) - ordinal;
    }
    return ordinal * (uint32_t) BSP_MEMORY_BIST_CHUNK_BYTES;
}


/// <summary>
///     Rolls the context forward by a completed slice. A write sweep becomes
///     its pair's verify sweep only when the final chunk has landed, and the
///     verify sweep completing moves to the next pair's write sweep -- the
///     whole-device separation that makes address aliasing observable.
/// </summary>
void BSP_MemoryBistAdvance( bsp_memory_bist_context_t *ptr_context, const uint32_t chunks )
{
    ptr_context->chunks_done += chunks;
    if ( ptr_context->chunks_done >= (uint32_t) BSP_MEMORY_BIST_SWEEP_CHUNKS )
    {
        ptr_context->chunks_done = 0u;
        if ( !ptr_context->verifying )
        {
            ptr_context->verifying = true;
        }
        else
        {
            ptr_context->verifying = false;
            ptr_context->pair += 1u;
        }
    }
}


/// <summary>
///     Judges one chunk of verify readback against the pattern, filling the
///     detailed records while capacity lasts and counting every mismatch
///     regardless, so a widespread failure still reports its true extent.
/// </summary>
void BSP_MemoryBistVerifyChunk( bsp_memory_bist_context_t *ptr_context, const uint32_t address,
                                const uint8_t *ptr_observed, const uint32_t count )
{
    for ( uint32_t byte = 0u; byte < count; ++byte )
    {
        const uint8_t expected = BSP_MemoryBistExpectedByte( ptr_context->pair, address + byte );
        const uint8_t actual = ptr_observed[ byte ];
        if ( actual == expected )
        {
            continue;
        }
        ptr_context->log.total += 1u;
        if ( ptr_context->log.recorded < (uint32_t) BSP_MEMORY_BIST_ERROR_CAPACITY )
        {
            bsp_memory_bist_error_t *ptr_record =
                &ptr_context->log.records[ ptr_context->log.recorded ];
            ptr_record->pair = (uint8_t) ptr_context->pair;
            ptr_record->page = (uint16_t) ( ( address + byte ) >> BSP_MEMORY_BIST_PAGE_SHIFT );
            ptr_record->address = address + byte;
            ptr_record->expected = expected;
            ptr_record->actual = actual;
            ptr_record->xor_bits = expected ^ actual;
            ptr_context->log.recorded += 1u;
        }
    }
}


/// <summary>
///     Watches a verify slice for the bus-failure signature: every byte one
///     uniform 0x00 or 0xFF that the current pattern cannot produce across a
///     whole slice. The address-derived pairs are never uniform, and the fixed
///     pairs are excused only when the constant is their own byte.
/// </summary>
void BSP_MemoryBistNoteSliceObserved( bsp_memory_bist_context_t *ptr_context,
                                      const uint8_t *ptr_observed, const uint32_t bytes )
{
    if ( bytes == 0u )
    {
        return;
    }

    const uint8_t first = ptr_observed[ 0 ];
    if ( first != 0x00u && first != 0xffu )
    {
        return;
    }
    for ( uint32_t byte = 1u; byte < bytes; ++byte )
    {
        if ( ptr_observed[ byte ] != first )
        {
            return;
        }
    }
    if ( ptr_context->pair > 1u && _fixedPattern[ ptr_context->pair ] == first )
    {
        return;
    }
    ptr_context->dead_bus = true;
}


/// <summary>
///     Sums the bus work already completed, counting each pair as two full
///     device passes.
/// </summary>
/// <returns>
///     Bytes written plus bytes verified so far.
/// </returns>
uint32_t BSP_MemoryBistProgressBytes( const bsp_memory_bist_context_t *ptr_context )
{
    const uint32_t sweeps = ( ptr_context->pair * 2u ) + ( ptr_context->verifying ? 1u : 0u );
    return ( sweeps * (uint32_t) BSP_MEMORY_BIST_DEVICE_BYTES ) +
           ( ptr_context->chunks_done * (uint32_t) BSP_MEMORY_BIST_CHUNK_BYTES );
}


/// <summary>
///     Delivers the verdict. A dead bus or a failed window restore is a
///     controller failure whatever the mismatch count says. An alias verdict
///     requires every recorded address-derived mismatch to read exactly what a
///     single flipped address bit would have stored there -- one such match
///     alone is an 8-percent accident for a random wrong byte, but a decode
///     fault explains every mismatch it makes, so unanimity is the signature.
///     Everything else is a data failure.
/// </summary>
/// <returns>
///     PASS, DATA_FAIL, ALIAS_FAIL or CONTROLLER_FAIL.
/// </returns>
bsp_memory_bist_result BSP_MemoryBistClassify( const bsp_memory_bist_context_t *ptr_context,
                                               const bool restored )
{
    if ( ptr_context->dead_bus || !restored )
    {
        return BSP_MEMORY_BIST_CONTROLLER_FAIL;
    }
    if ( ptr_context->log.total == 0u )
    {
        return BSP_MEMORY_BIST_PASS;
    }

    bool any_address_derived = false;
    bool all_aliased = true;
    for ( uint32_t index = 0u; index < ptr_context->log.recorded; ++index )
    {
        const bsp_memory_bist_error_t *ptr_record = &ptr_context->log.records[ index ];
        if ( ptr_record->pair > 1u )
        {
            /* Only the address-derived pairs can name the address a stray
               byte came from; a fixed pattern reads the same everywhere. */
            continue;
        }
        any_address_derived = true;
        bool aliased = false;
        for ( uint32_t bit = 0u; bit < ADDRESS_BITS; ++bit )
        {
            const uint32_t alias = ptr_record->address ^ ( 1u << bit );
            if ( ptr_record->actual == BSP_MemoryBistExpectedByte( ptr_record->pair, alias ) )
            {
                aliased = true;
                break;
            }
        }
        all_aliased = all_aliased && aliased;
    }
    if ( any_address_derived && all_aliased )
    {
        return BSP_MEMORY_BIST_ALIAS_FAIL;
    }
    return BSP_MEMORY_BIST_DATA_FAIL;
}
