#ifndef FORGIX_BSP_MEMORY_BIST_LOGIC_H
#define FORGIX_BSP_MEMORY_BIST_LOGIC_H

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
** Enumerated Values, Type Definitions
**
***************************************************************************************/


enum
{
    /* The APS1604M-3SQR as the datasheet draws it: 2 MiBytes as 2M x 8,
       addresses 0x000000..0x1FFFFF, 512-byte pages. */
    BSP_MEMORY_BIST_DEVICE_BYTES = 2 * 1024 * 1024,
    BSP_MEMORY_BIST_PAGE_SHIFT = 9,

    /* Sixteen-byte logical chunks keep fault records and address-order planning
       compact. Hardware groups 256 of them into one 4 KiB mapped-QPI slice. */
    BSP_MEMORY_BIST_CHUNK_BYTES = 16,
    BSP_MEMORY_BIST_SWEEP_CHUNKS = BSP_MEMORY_BIST_DEVICE_BYTES / BSP_MEMORY_BIST_CHUNK_BYTES,

    /* One foreground pass: small enough to return comfortably inside the
       watchdog window while USB and flash XIP continue normally. */
    BSP_MEMORY_BIST_SLICE_CHUNKS = 256,

    /* Twenty-three write-then-verify pattern pairs; see the pair table in the
       source. The last pair doubles as the final clear, so a completed run
       always leaves the device zeroed. */
    BSP_MEMORY_BIST_PAIRS = 23,

    /* Detailed records kept for the first mismatches; the count keeps rising
       past this so a widespread failure still reports its true size. */
    BSP_MEMORY_BIST_ERROR_CAPACITY = 8,
};


/* Every way the runtime memory test can end. The alias verdict exists apart
   from plain data failure because they indict different hardware: a data
   failure is a cell or a line, an alias failure is the address decode --
   typically a smaller or miswired die answering a 2 MiByte window. */
typedef enum bsp_memory_bist_result_tag
{
    BSP_MEMORY_BIST_PASS = 0,
    BSP_MEMORY_BIST_DATA_FAIL,
    BSP_MEMORY_BIST_ALIAS_FAIL,
    BSP_MEMORY_BIST_CONTROLLER_FAIL,
    BSP_MEMORY_BIST_SKIPPED, /* built without PSRAM support */
} bsp_memory_bist_result;


/* One mismatch, with everything a fault map needs: which pattern, which page,
   the absolute address, and the exact bit difference. */
typedef struct bsp_memory_bist_error_t_tag
{
    uint8_t pair;
    uint16_t page; /* address >> BSP_MEMORY_BIST_PAGE_SHIFT */
    uint32_t address;
    uint8_t expected;
    uint8_t actual;
    uint8_t xor_bits;
} bsp_memory_bist_error_t;


typedef struct bsp_memory_bist_log_t_tag
{
    uint32_t total;    /* every mismatch seen, past the recorded capacity */
    uint32_t recorded; /* how many of records[] are filled */
    bsp_memory_bist_error_t records[ BSP_MEMORY_BIST_ERROR_CAPACITY ];
} bsp_memory_bist_log_t;


/* Where the test stands. Zero-initialised it is the start of pair 0's write
   sweep; pair reaching BSP_MEMORY_BIST_PAIRS is completion. */
typedef struct bsp_memory_bist_context_t_tag
{
    uint32_t pair;
    bool verifying;       /* false: write sweep; true: verify sweep */
    uint32_t chunks_done; /* within the current sweep */
    bool dead_bus;        /* a verify slice was uniformly 0x00 or 0xFF wrongly */
    bsp_memory_bist_log_t log;
} bsp_memory_bist_context_t;


/* One foreground pass's worth of work, described but not yet performed. */
typedef struct bsp_memory_bist_slice_t_tag
{
    bool done; /* nothing left to run; every other field is meaningless */
    uint32_t pair;
    bool verifying;
    uint32_t first_chunk; /* ordinal within the sweep */
    uint32_t chunk_count;
    bool descending; /* issue order for this pair's sweeps */
} bsp_memory_bist_slice_t;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


/* The single source of truth for every pattern byte: what the write sweep
   stores at an address is exactly what the verify sweep later demands of it,
   both derived from the pair number and the absolute address alone. */
uint8_t BSP_MemoryBistExpectedByte( const uint32_t pair, const uint32_t address );

/* A short label for progress lines: "addr", "~addr", or the fixed byte. */
const char *BSP_MemoryBistPairLabel( const uint32_t pair );

/* Fills one chunk's worth of pattern bytes starting at the given address. */
void BSP_MemoryBistFillChunk( const uint32_t pair, const uint32_t address, uint8_t *ptr_data,
                              const uint32_t count );

/* Describes the next slice of work without advancing anything. */
bsp_memory_bist_slice_t BSP_MemoryBistNextSlice( const bsp_memory_bist_context_t *ptr_context );

/* The bus address of one chunk of a slice, honouring the pair's issue order:
   descending pairs run their chunks from the top of the device down, while
   the bytes inside a chunk always ascend because a bus burst does. */
uint32_t BSP_MemoryBistChunkAddress( const bsp_memory_bist_slice_t *ptr_slice,
                                     const uint32_t index );

/* Moves the context forward by a completed slice: write sweeps roll into their
   verify sweeps only after every chunk has been written, which is the
   separation that lets a later write corrupt an earlier address and be seen. */
void BSP_MemoryBistAdvance( bsp_memory_bist_context_t *ptr_context, const uint32_t chunks );

/* Judges one verified chunk's readback, recording the first few mismatches in
   detail and counting the rest. */
void BSP_MemoryBistVerifyChunk( bsp_memory_bist_context_t *ptr_context, const uint32_t address,
                                const uint8_t *ptr_observed, const uint32_t count );

/* The dead-bus detector: a verify slice returning one uniform 0x00 or 0xFF
   that the pattern cannot explain is a controller or bus failure, not a
   memory result, and continuing would only manufacture two million
   meaningless mismatches. */
void BSP_MemoryBistNoteSliceObserved( bsp_memory_bist_context_t *ptr_context,
                                      const uint8_t *ptr_observed, const uint32_t bytes );

/* Total bytes of bus work completed, for progress reporting. */
uint32_t BSP_MemoryBistProgressBytes( const bsp_memory_bist_context_t *ptr_context );

/* The final verdict once the run has ended or been cut short. */
bsp_memory_bist_result BSP_MemoryBistClassify( const bsp_memory_bist_context_t *ptr_context,
                                               const bool restored );

#ifdef __cplusplus
}
#endif

#endif
