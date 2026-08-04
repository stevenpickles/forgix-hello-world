/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_fpga.h"

#include "fpga_image.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include <stddef.h>




/***************************************************************************************
**
** Compiler Define Directives
**
***************************************************************************************/


/* The register the FPGA reports its own status through -- cdone/config state
   as seen from inside the design rather than off the CDONE pin. */
#define REG_STATUS ( (uint8_t) 0x01 )

/* The FPGA's free-running 32 MHz tick counter. A write to the capture address
   latches all 32 bits into a snapshot read back one byte per transaction at
   TICK_0..TICK_3; the latch is what keeps the four reads describing one
   instant over a bus that moves a single byte at a time. */
#define REG_TICK_CAPTURE ( (uint8_t) 0x30 )
#define REG_TICK_0 ( (uint8_t) 0x30 )
#define REG_TICK_1 ( (uint8_t) 0x31 )
#define REG_TICK_2 ( (uint8_t) 0x32 )
#define REG_TICK_3 ( (uint8_t) 0x33 )




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


/* The physical pin assignment for the bit-banged link between the RP2350 and
   the FPGA: which GPIO carries chip select, clock, the shared data line, and
   the reset/handshake/oscillator-enable signals that bring the FPGA up. */
typedef enum bsp_fpga_pin_tag
{
    PIN_CS = 1,
    PIN_SCK = 2,
    PIN_SDIO = 3,
    PIN_CRESET_N = 4,
    PIN_CDONE = 5,
    PIN_STATUS = 6,
    PIN_OSC_EN = 19,
} bsp_fpga_pin;

/* Opcodes understood by the FPGA's register protocol, sent as the first byte
   of a transaction to select whether the bytes that follow write a register,
   read one back, reset the design, or just ping it for its design ID. */
typedef enum bsp_fpga_command_tag
{
    CMD_WRITE = 0x02,
    CMD_READ = 0x03,
    CMD_RESET = 0x7f,
    CMD_PING = 0x9f,
} bsp_fpga_command;




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* Whether the last configuration attempt ended with the FPGA answering a ping
   with the expected design ID. Cached so callers can ask without clocking the
   bus, which matters in the foreground loop. */
static bool _fpgaReady;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static void _RuntimeBusIdle( void );

static bool _Configure( void );

static void _SendByte( const uint8_t value, const bool releaseAfterSample );

static uint8_t _ReceiveByte( void );

static uint8_t _Transaction( const uint8_t *const tx, const uint32_t count, const bool read );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Configures the FPGA from the embedded bitstream, then waits 1500 ms before
///     pinging it. The delay is not politeness: CDONE rises before the design's
///     own logic is ready to answer, and pinging too early reads back nonsense.
/// </summary>
/// <returns>
///     Everything bring-up learned, including the raw pins, so a caller can tell
///     an unconfigured FPGA from a configured one answering wrongly.
/// </returns>
bsp_fpga_init_result_t BSP_FpgaInit( void )
{
    bsp_fpga_init_result_t result = { 0 };
    result.configured = _Configure();

    sleep_ms( 1500 );
    if ( result.configured )
    {
        result.design_id = BSP_FpgaPing();
    }
    else
    {
        result.design_id = 0;
    }
    result.ready = result.configured && result.design_id == BSP_FPGA_DESIGN_ID;
    result.cdone = gpio_get( PIN_CDONE );
    result.status_pin = gpio_get( PIN_STATUS );
    _fpgaReady = result.ready;
    return result;
}

/// <summary>
///     Reloads the bitstream after a runtime fault and revalidates the design ID.
///     A full re-run of bring-up rather than a reset, because a lost
///     configuration cannot be recovered by resetting the design.
/// </summary>
/// <returns>
///     True when the FPGA is answering as the expected design again.
/// </returns>
bool BSP_FpgaReconfigure( void )
{
    const bsp_fpga_init_result_t result = BSP_FpgaInit();
    return result.ready;
}

/// <summary>
///     Whether this image was built to attempt recovery after a runtime FPGA
///     fault. Reported as a value rather than left as a compile switch so both
///     policies stay reachable from the application layer, and testable.
/// </summary>
/// <returns>
///     True when FORGIX_FPGA_AUTO_RECONFIGURE was set at build time.
/// </returns>
bool BSP_FpgaAutoReconfigureEnabled( void )
{
#if FORGIX_FPGA_AUTO_RECONFIGURE
    return true;
#else
    return false;
#endif
}

/// <summary>
///     The cached result of the last bring-up, not a live probe. Cheap enough for
///     the foreground loop, which is the point -- asking the FPGA directly every
///     iteration would clock the bit-banged bus for no new information.
/// </summary>
/// <returns>
///     True if the last configuration attempt ended with the expected design ID.
/// </returns>
bool BSP_FpgaIsReady( void )
{
    return _fpgaReady;
}

/// <summary>
///     Reads the configuration-done pin live. Low at runtime means the FPGA lost
///     its configuration, which the diagnostics layer treats as a recoverable
///     hardware fault rather than a bus error.
/// </summary>
/// <returns>
///     True while the FPGA reports itself configured.
/// </returns>
bool BSP_FpgaCdone( void )
{
    return gpio_get( PIN_CDONE );
}

/// <summary>
///     Asks the design to identify itself. The cheapest evidence that the link is
///     working end to end, since a wrong answer implicates the design and a
///     garbled one implicates the bus.
/// </summary>
/// <returns>
///     The design's identity byte, to be compared against BSP_FPGA_DESIGN_ID.
/// </returns>
uint8_t BSP_FpgaPing( void )
{
    const uint8_t tx[] = { CMD_PING };
    return _Transaction( tx, 1, true );
}

/// <summary>
///     Reads the status register, which is the design's own view of its
///     configuration state -- distinct from BSP_FpgaCdone, which reads the
///     physical pin. They can disagree, and that disagreement is diagnostic.
/// </summary>
/// <returns>
///     The status register contents.
/// </returns>
uint8_t BSP_FpgaReadStatus( void )
{
    return BSP_FpgaReadRegister( REG_STATUS );
}

/// <summary>
///     Reads the status pin directly, bypassing the register protocol. Usable when
///     the bus itself is suspect, since it needs no transaction.
/// </summary>
/// <returns>
///     The pin level.
/// </returns>
bool BSP_FpgaStatusPin( void )
{
    return gpio_get( PIN_STATUS );
}

/// <summary>
///     R
/// </summary>
/// <returns>
///     e
/// </returns>
void BSP_FpgaReset( void )
{
    const uint8_t tx[] = { CMD_RESET };
    _Transaction( tx, 1, false );
}

/// <summary>
///     Reads one register. Every read clocks the bit-banged bus, so this is not
///     free in the foreground loop.
/// </summary>
/// <returns>
///     The register contents, or whatever the bus returned if the FPGA is not
///     answering -- there is no in-band way to tell those apart.
/// </returns>
uint8_t BSP_FpgaReadRegister( const uint8_t address )
{
    const uint8_t tx[] = { CMD_READ, address };
    return _Transaction( tx, 2, true );
}

/// <summary>
///     W
/// </summary>
/// <returns>
///     r
/// </returns>
void BSP_FpgaWriteRegister( const uint8_t address, const uint8_t value )
{
    const uint8_t tx[] = { CMD_WRITE, address, value };
    _Transaction( tx, 3, false );
}

/// <summary>
///     Latches the FPGA's free-running 32 MHz counter and reads the snapshot
///     back. The capture write is the sampling instant; the four byte reads
///     that follow can take as long as they like without tearing the value.
///     Five transactions of bit-banged bus, so a few hundred microseconds.
/// </summary>
/// <returns>
///     The latched counter value, which wraps about every 134 seconds -- or
///     junk if the FPGA is not answering, which no in-band check can tell
///     apart from a real count.
/// </returns>
uint32_t BSP_FpgaTickSample( void )
{
    BSP_FpgaWriteRegister( REG_TICK_CAPTURE, 0u );
    return (uint32_t) BSP_FpgaReadRegister( REG_TICK_0 ) |
           ( (uint32_t) BSP_FpgaReadRegister( REG_TICK_1 ) << 8u ) |
           ( (uint32_t) BSP_FpgaReadRegister( REG_TICK_2 ) << 16u ) |
           ( (uint32_t) BSP_FpgaReadRegister( REG_TICK_3 ) << 24u );
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/// <summary>
///     Hands the bus back to bit-banged GPIO after configuration, parking chip
///     select high and the clock low. The SPI peripheral is torn down because the
///     register protocol needs the data line to turn around mid-transaction,
///     which the peripheral cannot do.
/// </summary>
static void _RuntimeBusIdle( void )
{
    spi_deinit( spi0 );
    gpio_init( PIN_CS );
    gpio_set_dir( PIN_CS, GPIO_OUT );
    gpio_put( PIN_CS, 1 );
    gpio_init( PIN_SCK );
    gpio_set_dir( PIN_SCK, GPIO_OUT );
    gpio_put( PIN_SCK, 0 );
    gpio_init( PIN_SDIO );
    gpio_set_dir( PIN_SDIO, GPIO_IN );
}

/// <summary>
///     Clocks the bitstream in over hardware SPI, then waits up to 500 ms for
///     CDONE. The 32 trailing zero bytes are required by the FPGA to finish
///     internal startup after the last bitstream byte.
/// </summary>
/// <returns>
///     True if CDONE rose before the deadline.
/// </returns>
static bool _Configure( void )
{
    gpio_init( PIN_OSC_EN );
    gpio_set_dir( PIN_OSC_EN, GPIO_OUT );
    gpio_put( PIN_OSC_EN, 1 );
    gpio_init( PIN_CRESET_N );
    gpio_set_dir( PIN_CRESET_N, GPIO_OUT );
    gpio_put( PIN_CRESET_N, 0 );
    gpio_init( PIN_CDONE );
    gpio_set_dir( PIN_CDONE, GPIO_IN );
    gpio_init( PIN_STATUS );
    gpio_set_dir( PIN_STATUS, GPIO_IN );
    gpio_init( PIN_CS );
    gpio_set_dir( PIN_CS, GPIO_OUT );
    gpio_put( PIN_CS, 0 );
    gpio_set_function( PIN_SCK, GPIO_FUNC_SPI );
    gpio_set_function( PIN_SDIO, GPIO_FUNC_SPI );
    spi_init( spi0, 8 * 1000 * 1000 );
    spi_set_format( spi0, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST );

    sleep_ms( 2 );
    gpio_put( PIN_CRESET_N, 1 );
    sleep_ms( 5 );
    spi_write_blocking( spi0, fpga_image, fpga_image_size );

    const uint8_t trailing[ 32 ] = { 0 };
    spi_write_blocking( spi0, trailing, sizeof trailing );

    const absolute_time_t deadline = make_timeout_time_ms( 500 );
    bool done = false;
    while ( !time_reached( deadline ) )
    {
        if ( gpio_get( PIN_CDONE ) )
        {
            done = true;
            break;
        }
        sleep_ms( 1 );
    }

    gpio_put( PIN_CS, 1 );
    _RuntimeBusIdle();
    return done;
}

/// <summary>
///     Clocks out one byte, most significant bit first. releaseAfterSample turns
///     the data line around on the final bit so the FPGA can drive its reply
///     without a contended cycle between the two.
/// </summary>
static void _SendByte( const uint8_t value, const bool releaseAfterSample )
{
    gpio_set_dir( PIN_SDIO, GPIO_OUT );
    for ( int32_t bit = 7; bit >= 0; --bit )
    {
        gpio_put( PIN_SDIO, ( value >> bit ) & 1u );
        gpio_put( PIN_SCK, 1 );
        busy_wait_us_32( 1 );
        if ( releaseAfterSample && bit == 0 )
        {
            gpio_set_dir( PIN_SDIO, GPIO_IN );
        }
        gpio_put( PIN_SCK, 0 );
        busy_wait_us_32( 1 );
    }
}

/// <summary>
///     Clocks in one byte with the data line already turned around by the caller.
/// </summary>
/// <returns>
///     The byte sampled off the wire.
/// </returns>
static uint8_t _ReceiveByte( void )
{
    uint8_t value = 0;
    gpio_set_dir( PIN_SDIO, GPIO_IN );
    busy_wait_us_32( 1 );
    for ( uint32_t bit = 0; bit < 8; ++bit )
    {
        gpio_put( PIN_SCK, 1 );
        busy_wait_us_32( 1 );
        value = (uint8_t) ( ( value << 1 ) | gpio_get( PIN_SDIO ) );
        gpio_put( PIN_SCK, 0 );
        busy_wait_us_32( 1 );
    }
    return value;
}

/// <summary>
///     Frames one register-protocol exchange between chip select edges. On a read
///     the turnaround is requested on the last outgoing byte, so the line is
///     already an input by the time the FPGA drives it.
/// </summary>
/// <returns>
///     The byte read, or zero on a write.
/// </returns>
static uint8_t _Transaction( const uint8_t *const tx, const uint32_t count, const bool read )
{
    gpio_put( PIN_CS, 0 );
    busy_wait_us_32( 1 );
    for ( uint32_t index = 0; index < count; ++index )
    {
        _SendByte( tx[ index ], read && index + 1 == count );
    }
    uint8_t result = 0;
    if ( read )
    {
        result = _ReceiveByte();
    }
    gpio_put( PIN_CS, 1 );
    gpio_set_dir( PIN_SDIO, GPIO_IN );
    busy_wait_us_32( 1 );
    return result;
}
