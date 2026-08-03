#include "application_effects.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "application_diagnostics.h"
#include "bsp.h"

enum {
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
typedef enum {
    LEVEL_FULL,
    LEVEL_RISING,
    LEVEL_FALLING,
    LEVEL_ZERO,
} level_t;

typedef struct {
    uint32_t current_time_ms;
    uint32_t started_ms;
    uint32_t next_update_ms;
    uint32_t update_ms;
    uint32_t tick;
    bool ready;
    bool saved;
    bsp_led_state_t before;
} effects_state_t;

static effects_state_t effects;

static const level_t WHEEL[WHEEL_SECTORS][3] = {
    {LEVEL_FULL, LEVEL_RISING, LEVEL_ZERO},    /* red to yellow */
    {LEVEL_FALLING, LEVEL_FULL, LEVEL_ZERO},   /* yellow to green */
    {LEVEL_ZERO, LEVEL_FULL, LEVEL_RISING},    /* green to cyan */
    {LEVEL_ZERO, LEVEL_FALLING, LEVEL_FULL},   /* cyan to blue */
    {LEVEL_RISING, LEVEL_ZERO, LEVEL_FULL},    /* blue to magenta */
    {LEVEL_FULL, LEVEL_ZERO, LEVEL_FALLING},   /* magenta back to red */
};

/* One beat and a weaker echo, the shape a pulse actually has. A plain triangle
   ramp reads as breathing rather than a heartbeat. */
static const uint8_t HEARTBEAT[] = {
    16, 90, 200, 255, 190, 90, 32, 16, 24, 120, 170, 120, 48, 16, 16, 16,
};

/* Cold greens and blues drifting through a violet, which is what makes it read
   as an aurora rather than a colour fade. */
static const uint8_t AURORA[][3] = {
    {0, 40, 80}, {0, 120, 140}, {40, 0, 190}, {0, 190, 110}, {0, 40, 80},
};

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static bool update_due(void) {
    if (!deadline_reached(effects.current_time_ms, effects.next_update_ms)) {
        return false;
    }
    effects.next_update_ms = effects.current_time_ms + effects.update_ms;
    return true;
}

static uint8_t channel(level_t behaviour, uint8_t rising) {
    if (behaviour == LEVEL_FULL) {
        return 255u;
    }
    if (behaviour == LEVEL_RISING) {
        return rising;
    }
    if (behaviour == LEVEL_FALLING) {
        return (uint8_t)(255u - rising);
    }
    return 0u;
}

/* Saved before anything is driven and put back on the way out, so a user who
   asked for a light show does not get their colour taken away by it. */
static void begin(uint32_t update_ms) {
    effects.ready = BSP_FpgaIsReady();
    effects.current_time_ms = BSP_TimeNowMs();
    effects.started_ms = effects.current_time_ms;
    effects.next_update_ms = effects.current_time_ms;
    effects.update_ms = update_ms;
    effects.tick = 0;
    effects.saved = false;

    if (!effects.ready) {
        BSP_WatchdogMarkerSet(APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE);
        BSP_ConsolePuts("the LED is behind the FPGA, which is not responding");
        return;
    }
    effects.before = BSP_LedGet();
    effects.saved = true;
}

static void restore(void) {
    if (effects.saved) {
        BSP_LedSet(effects.before.red, effects.before.green, effects.before.blue,
                   effects.before.brightness);
        effects.saved = false;
    }
}

static void blinker_start(void) {
    BSP_WatchdogMarkerSet(APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE);
    BSP_ConsolePuts("\nblinker: red, green, blue at 1 Hz; press any key to stop");
    begin(BLINKER_STEP_MS);
}

static bool blinker_poll(void) {
    static const uint8_t SEQUENCE[][3] = {
        {255, 0, 0}, {0, 0, 0}, {0, 255, 0}, {0, 0, 0}, {0, 0, 255}, {0, 0, 0},
    };

    BSP_WatchdogMarkerSet(APPLICATION_DIAGNOSTICS_MARKER_EFFECT);
    effects.current_time_ms = BSP_TimeNowMs();
    if (!effects.ready) {
        return false;
    }
    if (update_due()) {
        const uint8_t *colour = SEQUENCE[effects.tick % 6u];
        BSP_LedSet(colour[0], colour[1], colour[2], EFFECT_BRIGHTNESS);
        ++effects.tick;
    }
    return true;
}

static void advanced_start(void) {
    BSP_WatchdogMarkerSet(APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE);
    BSP_ConsolePuts("\nadvanced blinker: heartbeat, colour wheel, aurora; press any key to stop");
    begin(ADVANCED_STEP_MS);
}

static void show_heartbeat(uint32_t elapsed_ms) {
    const uint32_t frames = (uint32_t)(sizeof HEARTBEAT / sizeof HEARTBEAT[0]);
    const uint8_t brightness = HEARTBEAT[(elapsed_ms / 60u) % frames];

    BSP_LedSet(255, 24, 24, brightness);
}

static void show_wheel(uint32_t elapsed_ms) {
    /* Two full turns across the phase, which is fast enough to read as a sweep
       and slow enough that a channel stuck on is obvious. */
    const uint32_t position = (elapsed_ms * WHEEL_SECTORS * WHEEL_SECTOR_STEPS * 2u) / WHEEL_MS;
    const uint32_t sector = (position / WHEEL_SECTOR_STEPS) % WHEEL_SECTORS;
    const uint8_t rising = (uint8_t)(position % WHEEL_SECTOR_STEPS);

    BSP_LedSet(channel(WHEEL[sector][0], rising), channel(WHEEL[sector][1], rising),
               channel(WHEEL[sector][2], rising), EFFECT_BRIGHTNESS);
}

static void show_aurora(uint32_t elapsed_ms) {
    const uint32_t stops = (uint32_t)(sizeof AURORA / sizeof AURORA[0]) - 1u;
    const uint32_t span_ms = AURORA_MS / stops;
    const uint32_t stop = elapsed_ms / span_ms;
    const uint32_t into = elapsed_ms % span_ms;
    uint8_t blended[3] = {0};

    for (uint32_t channelIndex = 0; channelIndex < 3u; ++channelIndex) {
        const int32_t from = AURORA[stop][channelIndex];
        const int32_t to = AURORA[stop + 1u][channelIndex];
        blended[channelIndex] = (uint8_t)(from + ((to - from) * (int32_t)into) / (int32_t)span_ms);
    }
    BSP_LedSet(blended[0], blended[1], blended[2], EFFECT_BRIGHTNESS);
}

static bool advanced_poll(void) {
    BSP_WatchdogMarkerSet(APPLICATION_DIAGNOSTICS_MARKER_EFFECT);
    effects.current_time_ms = BSP_TimeNowMs();
    if (!effects.ready) {
        return false;
    }

    const uint32_t elapsed_ms = effects.current_time_ms - effects.started_ms;
    if (elapsed_ms >= ADVANCED_TOTAL_MS) {
        restore();
        return false;
    }
    if (!update_due()) {
        return true;
    }

    if (elapsed_ms < HEARTBEAT_MS) {
        show_heartbeat(elapsed_ms);
    } else if (elapsed_ms < HEARTBEAT_MS + WHEEL_MS) {
        show_wheel(elapsed_ms - HEARTBEAT_MS);
    } else {
        show_aurora(elapsed_ms - HEARTBEAT_MS - WHEEL_MS);
    }
    return true;
}

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

const application_activity_t *application_effects_blinker(void) {
    return &BLINKER;
}

const application_activity_t *application_effects_advanced(void) {
    return &ADVANCED;
}
