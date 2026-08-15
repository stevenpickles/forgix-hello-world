/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp_memory_bist_logic.h"




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bsp_memory_bist_context_t fresh_context( void );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


void setUp( void )
{
}


void tearDown( void )
{
}


/* Hand-computed little-endian lanes of 0x00000000 ^ 0xA5A55A5A and its
   complement: the test would prove nothing if it derived them the same way
   the implementation does. */
void test_expected_byte_extracts_little_endian_lanes_of_the_address_pattern( void )
{
    TEST_ASSERT_EQUAL_UINT8( 0x5Au, BSP_MemoryBistExpectedByte( 0u, 0x000000u ) );
    TEST_ASSERT_EQUAL_UINT8( 0x5Au, BSP_MemoryBistExpectedByte( 0u, 0x000001u ) );
    TEST_ASSERT_EQUAL_UINT8( 0xA5u, BSP_MemoryBistExpectedByte( 0u, 0x000002u ) );
    TEST_ASSERT_EQUAL_UINT8( 0xA5u, BSP_MemoryBistExpectedByte( 0u, 0x000003u ) );
    /* the next word differs only in the bits its own address contributes */
    TEST_ASSERT_EQUAL_UINT8( 0x5Eu, BSP_MemoryBistExpectedByte( 0u, 0x000004u ) );
}


void test_expected_byte_inverts_the_pattern_for_pair_one( void )
{
    TEST_ASSERT_EQUAL_UINT8( 0xA5u, BSP_MemoryBistExpectedByte( 1u, 0x000000u ) );
    TEST_ASSERT_EQUAL_UINT8( 0xA5u, BSP_MemoryBistExpectedByte( 1u, 0x000001u ) );
    TEST_ASSERT_EQUAL_UINT8( 0x5Au, BSP_MemoryBistExpectedByte( 1u, 0x000002u ) );
    TEST_ASSERT_EQUAL_UINT8( 0x5Au, BSP_MemoryBistExpectedByte( 1u, 0x000003u ) );
}


/* 0x001FFFFC ^ 0xA5A55A5A = 0xA5BAA5A6, byte-wise from the top of the
   device, so the derivation is proven at both ends of the address space. */
void test_expected_byte_holds_at_the_top_of_the_device( void )
{
    TEST_ASSERT_EQUAL_UINT8( 0xA6u, BSP_MemoryBistExpectedByte( 0u, 0x1FFFFCu ) );
    TEST_ASSERT_EQUAL_UINT8( 0xA5u, BSP_MemoryBistExpectedByte( 0u, 0x1FFFFDu ) );
    TEST_ASSERT_EQUAL_UINT8( 0xBAu, BSP_MemoryBistExpectedByte( 0u, 0x1FFFFEu ) );
    TEST_ASSERT_EQUAL_UINT8( 0xA5u, BSP_MemoryBistExpectedByte( 0u, 0x1FFFFFu ) );
}


void test_expected_byte_serves_the_full_pair_table( void )
{
    const uint8_t fixed[ BSP_MEMORY_BIST_PAIRS ] = {
        0x5Au, 0xA5u, /* the address-derived pairs, at address 0 */
        0x00u, 0xFFu, 0xAAu, 0x55u, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x20u, 0x40u,
        0x80u, 0xFEu, 0xFDu, 0xFBu, 0xF7u, 0xEFu, 0xDFu, 0xBFu, 0x7Fu, 0x00u,
    };

    for ( uint32_t pair = 0u; pair < (uint32_t) BSP_MEMORY_BIST_PAIRS; ++pair )
    {
        TEST_ASSERT_EQUAL_UINT8( fixed[ pair ], BSP_MemoryBistExpectedByte( pair, 0u ) );
    }
    /* fixed patterns are address-independent */
    TEST_ASSERT_EQUAL_UINT8( 0xAAu, BSP_MemoryBistExpectedByte( 4u, 0x123456u ) );
}


void test_pair_labels_name_the_patterns( void )
{
    TEST_ASSERT_EQUAL_STRING( "addr", BSP_MemoryBistPairLabel( 0u ) );
    TEST_ASSERT_EQUAL_STRING( "~addr", BSP_MemoryBistPairLabel( 1u ) );
    TEST_ASSERT_EQUAL_STRING( "aa", BSP_MemoryBistPairLabel( 4u ) );
    TEST_ASSERT_EQUAL_STRING( "80", BSP_MemoryBistPairLabel( 13u ) );
    TEST_ASSERT_EQUAL_STRING( "clear", BSP_MemoryBistPairLabel( 22u ) );
}


void test_fill_chunk_agrees_with_expected_byte( void )
{
    uint8_t data[ BSP_MEMORY_BIST_CHUNK_BYTES ];

    BSP_MemoryBistFillChunk( 0u, 0x1FFFF0u, data, sizeof data );

    for ( uint32_t byte = 0u; byte < sizeof data; ++byte )
    {
        TEST_ASSERT_EQUAL_UINT8( BSP_MemoryBistExpectedByte( 0u, 0x1FFFF0u + byte ), data[ byte ] );
    }
}


void test_next_slice_starts_at_the_bottom_of_pair_zero_ascending( void )
{
    const bsp_memory_bist_context_t context = fresh_context();

    const bsp_memory_bist_slice_t slice = BSP_MemoryBistNextSlice( &context );

    TEST_ASSERT_FALSE( slice.done );
    TEST_ASSERT_EQUAL_UINT32( 0u, slice.pair );
    TEST_ASSERT_FALSE( slice.verifying );
    TEST_ASSERT_EQUAL_UINT32( 0u, slice.first_chunk );
    TEST_ASSERT_EQUAL_UINT32( BSP_MEMORY_BIST_SLICE_CHUNKS, slice.chunk_count );
    TEST_ASSERT_FALSE( slice.descending );
}


void test_next_slice_runs_odd_pairs_descending( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    context.pair = 1u;

    TEST_ASSERT_TRUE( BSP_MemoryBistNextSlice( &context ).descending );

    context.pair = 3u;
    TEST_ASSERT_TRUE( BSP_MemoryBistNextSlice( &context ).descending );

    context.pair = 22u;
    TEST_ASSERT_FALSE( BSP_MemoryBistNextSlice( &context ).descending );
}


void test_next_slice_shortens_the_tail_of_a_sweep( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    context.chunks_done = (uint32_t) BSP_MEMORY_BIST_SWEEP_CHUNKS - 10u;

    const bsp_memory_bist_slice_t slice = BSP_MemoryBistNextSlice( &context );

    TEST_ASSERT_EQUAL_UINT32( (uint32_t) BSP_MEMORY_BIST_SWEEP_CHUNKS - 10u, slice.first_chunk );
    TEST_ASSERT_EQUAL_UINT32( 10u, slice.chunk_count );
}


void test_next_slice_reports_done_after_the_last_pair( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    context.pair = (uint32_t) BSP_MEMORY_BIST_PAIRS;

    TEST_ASSERT_TRUE( BSP_MemoryBistNextSlice( &context ).done );
}


void test_chunk_addresses_ascend_for_even_pairs( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    const bsp_memory_bist_slice_t slice = BSP_MemoryBistNextSlice( &context );

    TEST_ASSERT_EQUAL_UINT32( 0x000000u, BSP_MemoryBistChunkAddress( &slice, 0u ) );
    TEST_ASSERT_EQUAL_UINT32( 0x000010u, BSP_MemoryBistChunkAddress( &slice, 1u ) );
}


void test_chunk_addresses_descend_from_the_top_for_odd_pairs( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    context.pair = 1u;
    const bsp_memory_bist_slice_t slice = BSP_MemoryBistNextSlice( &context );

    TEST_ASSERT_EQUAL_UINT32( 0x1FFFF0u, BSP_MemoryBistChunkAddress( &slice, 0u ) );
    TEST_ASSERT_EQUAL_UINT32( 0x1FFFE0u, BSP_MemoryBistChunkAddress( &slice, 1u ) );
}


void test_advance_walks_write_then_verify_then_the_next_pair( void )
{
    bsp_memory_bist_context_t context = fresh_context();

    BSP_MemoryBistAdvance( &context, (uint32_t) BSP_MEMORY_BIST_SLICE_CHUNKS );
    TEST_ASSERT_EQUAL_UINT32( (uint32_t) BSP_MEMORY_BIST_SLICE_CHUNKS, context.chunks_done );
    TEST_ASSERT_FALSE( context.verifying );

    /* completing the write sweep starts the SEPARATE verify sweep -- never a
       new pair, because whole-device write-before-verify is what exposes a
       later write corrupting an earlier address */
    BSP_MemoryBistAdvance( &context, (uint32_t) BSP_MEMORY_BIST_SWEEP_CHUNKS -
                                         (uint32_t) BSP_MEMORY_BIST_SLICE_CHUNKS );
    TEST_ASSERT_EQUAL_UINT32( 0u, context.chunks_done );
    TEST_ASSERT_TRUE( context.verifying );
    TEST_ASSERT_EQUAL_UINT32( 0u, context.pair );

    BSP_MemoryBistAdvance( &context, (uint32_t) BSP_MEMORY_BIST_SWEEP_CHUNKS );
    TEST_ASSERT_EQUAL_UINT32( 0u, context.chunks_done );
    TEST_ASSERT_FALSE( context.verifying );
    TEST_ASSERT_EQUAL_UINT32( 1u, context.pair );
}


void test_verify_chunk_accepts_a_faithful_readback( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    uint8_t observed[ BSP_MEMORY_BIST_CHUNK_BYTES ];
    BSP_MemoryBistFillChunk( 0u, 0x000100u, observed, sizeof observed );

    BSP_MemoryBistVerifyChunk( &context, 0x000100u, observed, sizeof observed );

    TEST_ASSERT_EQUAL_UINT32( 0u, context.log.total );
    TEST_ASSERT_EQUAL_UINT32( 0u, context.log.recorded );
}


void test_verify_chunk_records_a_mismatch_with_its_fault_map_fields( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    context.pair = 4u; /* fixed 0xAA */
    uint8_t observed[ BSP_MEMORY_BIST_CHUNK_BYTES ];
    memset( observed, 0xAA, sizeof observed );
    observed[ 3 ] = 0x2Au;

    BSP_MemoryBistVerifyChunk( &context, 0x000600u, observed, sizeof observed );

    TEST_ASSERT_EQUAL_UINT32( 1u, context.log.total );
    TEST_ASSERT_EQUAL_UINT32( 1u, context.log.recorded );
    TEST_ASSERT_EQUAL_UINT8( 4u, context.log.records[ 0 ].pair );
    TEST_ASSERT_EQUAL_UINT16( 3u, context.log.records[ 0 ].page );
    TEST_ASSERT_EQUAL_UINT32( 0x000603u, context.log.records[ 0 ].address );
    TEST_ASSERT_EQUAL_UINT8( 0xAAu, context.log.records[ 0 ].expected );
    TEST_ASSERT_EQUAL_UINT8( 0x2Au, context.log.records[ 0 ].actual );
    TEST_ASSERT_EQUAL_UINT8( 0x80u, context.log.records[ 0 ].xor_bits );
}


void test_verify_chunk_keeps_counting_after_the_records_fill( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    context.pair = 2u; /* fixed 0x00 */
    uint8_t observed[ BSP_MEMORY_BIST_CHUNK_BYTES ];
    memset( observed, 0x11, sizeof observed );

    BSP_MemoryBistVerifyChunk( &context, 0x000000u, observed, sizeof observed );

    TEST_ASSERT_EQUAL_UINT32( 16u, context.log.total );
    TEST_ASSERT_EQUAL_UINT32( (uint32_t) BSP_MEMORY_BIST_ERROR_CAPACITY, context.log.recorded );
    /* the ninth and later mismatches were counted, not recorded */
    TEST_ASSERT_EQUAL_UINT32( 0x000007u,
                              context.log.records[ BSP_MEMORY_BIST_ERROR_CAPACITY - 1 ].address );
}


void test_dead_bus_latches_on_a_uniform_readback_the_pattern_cannot_explain( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    uint8_t observed[ 64 ];

    memset( observed, 0x00, sizeof observed );
    BSP_MemoryBistNoteSliceObserved( &context, observed, sizeof observed );
    TEST_ASSERT_TRUE( context.dead_bus );

    context = fresh_context();
    memset( observed, 0xFF, sizeof observed );
    BSP_MemoryBistNoteSliceObserved( &context, observed, sizeof observed );
    TEST_ASSERT_TRUE( context.dead_bus );

    /* a fixed pattern whose own byte is not the constant seen */
    context = fresh_context();
    context.pair = 3u; /* expects 0xFF */
    memset( observed, 0x00, sizeof observed );
    BSP_MemoryBistNoteSliceObserved( &context, observed, sizeof observed );
    TEST_ASSERT_TRUE( context.dead_bus );
}


void test_dead_bus_excuses_patterns_that_really_are_uniform( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    uint8_t observed[ 64 ];

    context.pair = 2u; /* fixed 0x00 */
    memset( observed, 0x00, sizeof observed );
    BSP_MemoryBistNoteSliceObserved( &context, observed, sizeof observed );
    TEST_ASSERT_FALSE( context.dead_bus );

    context.pair = 3u; /* fixed 0xFF */
    memset( observed, 0xFF, sizeof observed );
    BSP_MemoryBistNoteSliceObserved( &context, observed, sizeof observed );
    TEST_ASSERT_FALSE( context.dead_bus );
}


void test_dead_bus_ignores_varied_or_non_stuck_readbacks( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    uint8_t observed[ 64 ];

    BSP_MemoryBistNoteSliceObserved( &context, observed, 0u );
    TEST_ASSERT_FALSE( context.dead_bus );

    memset( observed, 0x42, sizeof observed );
    BSP_MemoryBistNoteSliceObserved( &context, observed, sizeof observed );
    TEST_ASSERT_FALSE( context.dead_bus );

    memset( observed, 0x00, sizeof observed );
    observed[ 63 ] = 0x01u;
    BSP_MemoryBistNoteSliceObserved( &context, observed, sizeof observed );
    TEST_ASSERT_FALSE( context.dead_bus );
}


void test_progress_counts_completed_sweeps_and_the_partial_one( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    TEST_ASSERT_EQUAL_UINT32( 0u, BSP_MemoryBistProgressBytes( &context ) );

    context.pair = 1u;
    context.verifying = true;
    context.chunks_done = 2u;
    TEST_ASSERT_EQUAL_UINT32( ( 3u * (uint32_t) BSP_MEMORY_BIST_DEVICE_BYTES ) + 32u,
                              BSP_MemoryBistProgressBytes( &context ) );
}


void test_classify_passes_a_clean_restored_run( void )
{
    const bsp_memory_bist_context_t context = fresh_context();

    TEST_ASSERT_EQUAL( BSP_MEMORY_BIST_PASS, BSP_MemoryBistClassify( &context, true ) );
}


void test_classify_calls_a_dead_bus_or_failed_restore_controller_failure( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    context.dead_bus = true;
    TEST_ASSERT_EQUAL( BSP_MEMORY_BIST_CONTROLLER_FAIL, BSP_MemoryBistClassify( &context, true ) );

    const bsp_memory_bist_context_t clean = fresh_context();
    TEST_ASSERT_EQUAL( BSP_MEMORY_BIST_CONTROLLER_FAIL, BSP_MemoryBistClassify( &clean, false ) );
}


void test_classify_calls_fixed_pattern_mismatches_data_failure( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    context.pair = 4u;
    uint8_t observed[ 1 ] = { 0x2Au };

    BSP_MemoryBistVerifyChunk( &context, 0x000100u, observed, 1u );

    TEST_ASSERT_EQUAL( BSP_MEMORY_BIST_DATA_FAIL, BSP_MemoryBistClassify( &context, true ) );
}


/* Address 0 under pair 0 expects 0x5A; reading 0x4A there is exactly what the
   pattern stored at address 0x10, one flipped address bit away -- the decode
   signature. */
void test_classify_calls_a_single_bit_alias_signature_alias_failure( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    uint8_t observed[ 1 ] = { 0x4Au };

    BSP_MemoryBistVerifyChunk( &context, 0x000000u, observed, 1u );

    TEST_ASSERT_EQUAL( BSP_MEMORY_BIST_ALIAS_FAIL, BSP_MemoryBistClassify( &context, true ) );
}


/* 0xF0 at address 0 matches none of the 21 flipped-bit candidates, so one
   aliased-looking byte beside one that is not stays a data failure: a decode
   fault explains every mismatch it makes, and unanimity is the signature. */
void test_classify_requires_every_address_derived_error_to_fit_the_alias( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    uint8_t aliased[ 1 ] = { 0x4Au };
    uint8_t random[ 1 ] = { 0xF0u };

    BSP_MemoryBistVerifyChunk( &context, 0x000000u, aliased, 1u );
    BSP_MemoryBistVerifyChunk( &context, 0x000020u, random, 1u );
    /* a third mismatch after the verdict is already lost, so the unanimity
       check also runs from its failed state */
    BSP_MemoryBistVerifyChunk( &context, 0x000040u, random, 1u );

    TEST_ASSERT_EQUAL( BSP_MEMORY_BIST_DATA_FAIL, BSP_MemoryBistClassify( &context, true ) );
}


/* A fixed-pattern error in the log must not veto the alias verdict the
   address-derived errors establish: it cannot name an address either way. */
void test_classify_lets_fixed_pattern_errors_ride_along_with_an_alias( void )
{
    bsp_memory_bist_context_t context = fresh_context();
    uint8_t aliased[ 1 ] = { 0x4Au };

    BSP_MemoryBistVerifyChunk( &context, 0x000000u, aliased, 1u );
    context.pair = 4u;
    uint8_t fixed_wrong[ 1 ] = { 0x2Au };
    BSP_MemoryBistVerifyChunk( &context, 0x000100u, fixed_wrong, 1u );

    TEST_ASSERT_EQUAL( BSP_MEMORY_BIST_ALIAS_FAIL, BSP_MemoryBistClassify( &context, true ) );
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


static bsp_memory_bist_context_t fresh_context( void )
{
    bsp_memory_bist_context_t context;
    memset( &context, 0, sizeof context );
    return context;
}
