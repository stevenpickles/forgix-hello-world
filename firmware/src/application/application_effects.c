/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_effects.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "application_diagnostics.h"
#include "bsp.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


enum
{
    BLINKER_STEP_MS = 500,
    ADVANCED_STEP_MS = 50,
    HEARTBEAT_MS = 4000,
    WHEEL_MS = 6000,
    AURORA_MS = 8000,
    ADVANCED_TOTAL_MS = HEARTBEAT_MS + WHEEL_MS + AURORA_MS,
    /* Bright enough to be obvious across a room, dim enough not to be the first
       thing a user reaches to turn off. */
    EFFECT_BRIGHTNESS = 160,
    WHEEL_SECTOR_STEPS = 256,
    WHEEL_SECTORS = 6,
};


/* How a channel behaves across one sector of the colour wheel. Encoding the four
   behaviours rather than writing six triples of expressions keeps the sector
   table readable as the thing it is -- a loop around the hues. */
typedef enum
{
    LEVEL_FULL,
    LEVEL_RISING,
    LEVEL_FALLING,
    LEVEL_ZERO,
} level_t;


typedef struct
{
    uint32_t current_time_ms;
    uint32_t started_ms;
    uint32_t next_update_ms;
    uint32_t update_ms;
    uint32_t tick;
    bool ready;
    bool saved;
    bsp_led_state_t before;
} effects_state_t;




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


static effects_state_t effects;


static const level_t WHEEL[ WHEEL_SECTORS ][ 3 ] = {
    { LEVEL_FULL, LEVEL_RISING, LEVEL_ZERO },  /* red to yellow */
    { LEVEL_FALLING, LEVEL_FULL, LEVEL_ZERO }, /* yellow to green */
    { LEVEL_ZERO, LEVEL_FULL, LEVEL_RISING },  /* green to cyan */
    { LEVEL_ZERO, LEVEL_FALLING, LEVEL_FULL }, /* cyan to blue */
    { LEVEL_RISING, LEVEL_ZERO, LEVEL_FULL },  /* blue to magenta */
    { LEVEL_FULL, LEVEL_ZERO, LEVEL_FALLING }, /* magenta back to red */
};


/* One beat and a weaker echo, the shape a pulse actually has. A plain triangle
   ramp reads as breathing rather than a heartbeat. */
static const uint8_t HEARTBEAT[] = {
    16, 90, 200, 255, 190, 90, 32, 16, 24, 120, 170, 120, 48, 16, 16, 16,
};


/* Cold greens and blues drifting through a violet, which is what makes it read
   as an aurora rather than a colour fade. */
static const uint8_t AURORA[][ 3 ] = {
    { 0, 40, 80 }, { 0, 120, 140 }, { 40, 0, 190 }, { 0, 190, 110 }, { 0, 40, 80 },
};




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bool deadline_reached( uint32_t now_ms, uint32_t deadline_ms );

static bool update_due( void );

static uint8_t channel( level_t behaviour, uint8_t rising );

static void begin( uint32_t update_ms );

static void restore( void );

static void blinker_start( void );

static bool blinker_poll( void );

static void advanced_start( void );

static void show_heartbeat( uint32_t elapsed_ms );

static void show_wheel( uint32_t elapsed_ms );

static void show_aurora( uint32_t elapsed_ms );

static bool advanced_poll( void );


/* These name the static handlers above, so they have to sit after the
   declarations that supply them and before the public functions that hand them
   out -- the one place in the canonical section order both are true. */
static const application_activity_t BLINKER = {
    .name = "blinker",
    .start = blinker_start,
    .poll = blinker_poll,
    .stop = restore,
};

static const application_activity_t ADVANCED = {
    .name = "advanced blinker",
    .start = advanced_start,
    .poll = advanced_poll,
    .stop = restore,
};




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Hands out a pointer into this file's constant table, so it is always valid
///     and never null. Both activities here share one state block, though: only
///     the UI's one-at-a-time rule stops a second start from trampling the first.
/// </summary>
/// <returns>
///     The blinker activity, whose poll never retires it on its own.
/// </returns>
const application_activity_t *application_effects_blinker( void )
{
    return &BLINKER;
}

/// <summary>
///     The finite counterpart to the blinker, sharing its state block and its
///     stop handler. Its poll retires the activity once the three phases have
///     run, so the UI gets back to the menu without anyone pressing a key.
/// </summary>
/// <returns>
///     The advanced activity, which ends after ADVANCED_TOTAL_MS.
/// </returns>
const application_activity_t *application_effects_advanced( void )
{
    return &ADVANCED;
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/// <summary>
///     Subtracts and tests the sign instead of comparing the values, so an effect
///     started shortly before the 32-bit millisecond wrap keeps updating rather
///     than freezing on its current frame until the counter comes round again.
/// </summary>
/// <returns>
///     True once now_ms has reached deadline_ms.
/// </returns>
static bool deadline_reached( uint32_t now_ms, uint32_t deadline_ms )
{
    return (int32_t) ( now_ms - deadline_ms ) >= 0;
}

/// <summary>
///     Consumes the deadline as well as testing it, so a second call in the same
///     pass answers false and the caller must act on the first. The next deadline
///     is measured from now rather than from the one just met, so a pass that ran
///     late slips the cadence instead of firing twice to catch up.
/// </summary>
/// <returns>
///     True on the first call of each update period.
/// </returns>
static bool update_due( void )
{
    if ( !deadline_reached( effects.current_time_ms, effects.next_update_ms ) )
    {
        return false;
    }
    effects.next_update_ms = effects.current_time_ms + effects.update_ms;
    return true;
}

/// <summary>
///     LEVEL_FALLING is the exact complement of the rising ramp, which is what
///     makes the two channels of a sector cross without a step at the hue
///     boundary. The ramp is the sector position, not a brightness: the global
///     brightness register is set separately and this must not be scaled by it.
/// </summary>
/// <returns>
///     The channel level for this point in the sector, zero for LEVEL_ZERO.
/// </returns>
static uint8_t channel( level_t behaviour, uint8_t rising )
{
    if ( behaviour == LEVEL_FULL )
    {
        return 255u;
    }
    if ( behaviour == LEVEL_RISING )
    {
        return rising;
    }
    if ( behaviour == LEVEL_FALLING )
    {
        return (uint8_t) ( 255u - rising );
    }
    return 0u;
}

/* Saved before anything is driven and put back on the way out, so a user who
   asked for a light show does not get their colour taken away by it. */
/// <summary>
///     The first update falls due immediately, so an effect paints its opening
///     frame on the same pass it starts rather than after a dark step. An FPGA
///     that is not ready is reported and then left alone: the effect still starts
///     and still stops, but poll bails on the next pass and there is nothing
///     saved for restore to put back.
/// </summary>
static void begin( uint32_t update_ms )
{
    effects.ready = BSP_FpgaIsReady();
    effects.current_time_ms = BSP_TimeNowMs();
    effects.started_ms = effects.current_time_ms;
    effects.next_update_ms = effects.current_time_ms;
    effects.update_ms = update_ms;
    effects.tick = 0;
    effects.saved = false;

    if ( !effects.ready )
    {
        BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE );
        BSP_ConsolePuts( "the LED is behind the FPGA, which is not responding" );
        return;
    }
    effects.before = BSP_LedGet();
    effects.saved = true;
}

/// <summary>
///     Clears the saved flag on its way out, so the stop() the UI calls after a
///     poll that already restored does not write the LED a second time. Only the
///     colour and brightness come back -- BSP_LedSet latches the enable bit, so
///     an LED caught dark when it was saved returns lit until its owner writes it.
/// </summary>
static void restore( void )
{
    if ( effects.saved )
    {
        BSP_LedSet( effects.before.red, effects.before.green, effects.before.blue,
                    effects.before.brightness );
        effects.saved = false;
    }
}

/// <summary>
///     Announces before begin, so a user who asked for a light show is told what
///     they asked for even when the FPGA turns out to be missing and nothing will
///     light up. The console marker goes first because a wedged host pipe hangs
///     inside the write, and the post-reset report has to be able to say so.
/// </summary>
static void blinker_start( void )
{
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE );
    BSP_ConsolePuts( "\nblinker: red, green, blue at 1 Hz; press any key to stop" );
    begin( BLINKER_STEP_MS );
}

/// <summary>
///     Never ends of its own accord; only a keypress or an FPGA that was already
///     unresponsive at start stops it. The sequence interleaves blanks with
///     colours, so the 500 ms step gives one colour per second and the whole
///     cycle takes three -- the blanks are what make a repeat visible.
/// </summary>
/// <returns>
///     True to keep going; false only when the FPGA was never ready.
/// </returns>
static bool blinker_poll( void )
{
    static const uint8_t SEQUENCE[][ 3 ] = {
        { 255, 0, 0 }, { 0, 0, 0 }, { 0, 255, 0 }, { 0, 0, 0 }, { 0, 0, 255 }, { 0, 0, 0 },
    };

    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_EFFECT );
    effects.current_time_ms = BSP_TimeNowMs();
    if ( !effects.ready )
    {
        return false;
    }
    if ( update_due() )
    {
        const uint8_t *colour = SEQUENCE[ effects.tick % 6u ];
        BSP_LedSet( colour[ 0 ], colour[ 1 ], colour[ 2 ], EFFECT_BRIGHTNESS );
        ++effects.tick;
    }
    return true;
}

/// <summary>
///     The 50 ms step it asks for is a frame rate, not a phase length: the three
///     phases time themselves from elapsed milliseconds, so a coarser step makes
///     the same show choppier rather than shorter. Announces before begin for the
///     same reason blinker_start does.
/// </summary>
static void advanced_start( void )
{
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE );
    BSP_ConsolePuts( "\nadvanced blinker: heartbeat, colour wheel, aurora; press any key to stop" );
    begin( ADVANCED_STEP_MS );
}

/// <summary>
///     Holds one hue and moves brightness alone, which is what makes a pulse read
///     as a pulse instead of a colour fade. The frame index wraps, so the beat
///     repeats for as long as the phase lasts and elapsed_ms need not stay inside
///     the table. Its 60 ms frame is independent of the caller's step, so a
///     slower step drops frames rather than stretching the beat.
/// </summary>
static void show_heartbeat( uint32_t elapsed_ms )
{
    const uint32_t frames = (uint32_t) ( sizeof HEARTBEAT / sizeof HEARTBEAT[ 0 ] );
    const uint8_t brightness = HEARTBEAT[ ( elapsed_ms / 60u ) % frames ];

    BSP_LedSet( 255, 24, 24, brightness );
}

/// <summary>
///     Counts milliseconds from the start of its own phase, not of the show, and
///     keeps turning if handed more than the phase length -- the sector index
///     wraps and nothing clamps. Brightness is held at EFFECT_BRIGHTNESS
///     throughout, so hue is the only thing moving and a channel stuck on shows.
/// </summary>
static void show_wheel( uint32_t elapsed_ms )
{
    /* Two full turns across the phase, which is fast enough to read as a sweep
       and slow enough that a channel stuck on is obvious. */
    const uint32_t position = ( elapsed_ms * WHEEL_SECTORS * WHEEL_SECTOR_STEPS * 2u ) / WHEEL_MS;
    const uint32_t sector = ( position / WHEEL_SECTOR_STEPS ) % WHEEL_SECTORS;
    const uint8_t rising = (uint8_t) ( position % WHEEL_SECTOR_STEPS );

    BSP_LedSet( channel( WHEEL[ sector ][ 0 ], rising ), channel( WHEEL[ sector ][ 1 ], rising ),
                channel( WHEEL[ sector ][ 2 ], rising ), EFFECT_BRIGHTNESS );
}

/// <summary>
///     Blends between adjacent stops and so reads AURORA[ stop + 1 ]: unlike the
///     other two phases this one does not wrap, and elapsed_ms must stay strictly
///     below AURORA_MS or it indexes past the table. The blend runs in signed
///     arithmetic because a falling channel would otherwise underflow to white.
/// </summary>
static void show_aurora( uint32_t elapsed_ms )
{
    const uint32_t stops = (uint32_t) ( sizeof AURORA / sizeof AURORA[ 0 ] ) - 1u;
    const uint32_t span_ms = AURORA_MS / stops;
    const uint32_t stop = elapsed_ms / span_ms;
    const uint32_t into = elapsed_ms % span_ms;
    uint8_t blended[ 3 ] = { 0 };

    for ( uint32_t channel_index = 0; channel_index < 3u; ++channel_index )
    {
        const int32_t from = AURORA[ stop ][ channel_index ];
        const int32_t to = AURORA[ stop + 1u ][ channel_index ];
        blended[ channel_index ] =
            (uint8_t) ( from + ( ( to - from ) * (int32_t) into ) / (int32_t) span_ms );
    }
    BSP_LedSet( blended[ 0 ], blended[ 1 ], blended[ 2 ], EFFECT_BRIGHTNESS );
}

/// <summary>
///     Tests the total elapsed time before the frame deadline, so the show ends on
///     schedule rather than at the next 50 ms edge, and restores the LED itself on
///     the way out -- the stop() the UI then calls finds nothing left to undo.
///     Each phase is handed time relative to its own start, which is what keeps
///     the aurora blend inside its table.
/// </summary>
/// <returns>
///     False once the show has run out or the FPGA was never ready, true while
///     it wants another pass.
/// </returns>
static bool advanced_poll( void )
{
    BSP_WatchdogMarkerSet( APPLICATION_DIAGNOSTICS_MARKER_EFFECT );
    effects.current_time_ms = BSP_TimeNowMs();
    if ( !effects.ready )
    {
        return false;
    }

    const uint32_t elapsed_ms = effects.current_time_ms - effects.started_ms;
    if ( elapsed_ms >= ADVANCED_TOTAL_MS )
    {
        restore();
        return false;
    }
    if ( !update_due() )
    {
        return true;
    }

    if ( elapsed_ms < HEARTBEAT_MS )
    {
        show_heartbeat( elapsed_ms );
    }
    else if ( elapsed_ms < HEARTBEAT_MS + WHEEL_MS )
    {
        show_wheel( elapsed_ms - HEARTBEAT_MS );
    }
    else
    {
        show_aurora( elapsed_ms - HEARTBEAT_MS - WHEEL_MS );
    }
    return true;
}
