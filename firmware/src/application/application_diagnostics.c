#include "application_diagnostics.h"

#include <stdbool.h>
#include <stdint.h>

#include "bsp.h"

enum {
    HEARTBEAT_BRIGHTNESS = 64,
    /* Six heartbeat toggles, so three white on-phases, mark a successful FPGA
       reconfiguration. A frozen LED that resumes with this signature proves the
       MCU stayed alive and the FPGA lost its configuration. */
    RECOVERY_TOGGLES = 6,
    BOOT_BLINK_MAX = 8,
    /* The blink code is the USB-free image's only boot-evidence channel, and it
       plays once: a power cycle to see it again would destroy the very scratch
       registers it is reporting. So it is paced to be readable and repeated. */
    BOOT_BLINK_ON_MS = 350,
    BOOT_BLINK_OFF_MS = 250,
    BOOT_BLINK_GAP_MS = 800,
    BOOT_REPORT_REPEATS = 3,
};

/* Packing of snapshot slot 2. Slot 0 holds the loop-seconds counter and slot 1
   the raw USB activity counter. The FPGA counters are narrow modulo fields; the
   full-width values stay available live through the `diag` command. */
enum {
    HEALTH_FRAME_MASK = 0xffffu,
    HEALTH_CONNECTED_SHIFT = 16,
    HEALTH_SUSPENDED_SHIFT = 17,
    HEALTH_WRITE_BLOCKED_SHIFT = 18,
    HEALTH_FPGA_FAILURE_SHIFT = 19,
    HEALTH_FPGA_FAILURE_MASK = 0x7fu,
    HEALTH_FPGA_RECONFIGURE_SHIFT = 26,
    HEALTH_FPGA_RECONFIGURE_MASK = 0x3fu,
};

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint32_t blinks;
} boot_signature_t;

typedef struct {
    bool usb_present;
    bsp_boot_reason_t boot_reason;
    uint32_t boot_marker;
    uint32_t boot_snapshot[BSP_WATCHDOG_SNAPSHOT_SLOTS];

    bool led_on;
    uint32_t next_led_ms;
    uint32_t next_sample_ms;
    uint32_t uptime_seconds;
    uint32_t recovery_toggles;
    bsp_led_state_t commanded;

    bsp_usb_health_t health;
    uint32_t last_activity_count;
    uint32_t last_activity_ms;
    uint32_t last_frame_number;
    uint32_t last_frame_ms;

    uint32_t fpga_failures;
    uint32_t fpga_reconfigures;
} diagnostics_state_t;

static diagnostics_state_t diagnostics;

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static bool stalled_since(uint32_t now_ms, uint32_t since_ms) {
    return (int32_t)(now_ms - since_ms) >= (int32_t)APPLICATION_DIAGNOSTICS_STALL_TIMEOUT_MS;
}

/* The USB-free image has no USB health to show, so its resting heartbeat carries
   the last boot reason instead. The blink code plays once and cannot be replayed
   without destroying the evidence, so this keeps the verdict readable for the
   whole run: blue is nominal, any other resting color means something happened. */
static void resting_color(uint8_t *red, uint8_t *green, uint8_t *blue) {
    *red = 0;
    *green = 0;
    *blue = 255; /* blue: clean power-on */

    switch (diagnostics.boot_reason) {
    case BSP_BOOT_WATCHDOG:
        *red = 255;
        *blue = 0; /* red: the foreground stopped and the watchdog recovered it */
        break;
    case BSP_BOOT_BROWNOUT:
        *red = 255;
        *green = 255;
        *blue = 0; /* yellow: supply droop */
        break;
    case BSP_BOOT_OTHER:
        *green = 255; /* cyan: reset with no attributable cause */
        break;
    default:
        break;
    }
}

static void heartbeat_color(uint32_t now_ms, uint8_t *red, uint8_t *green, uint8_t *blue) {
    if (diagnostics.recovery_toggles) {
        *red = 255;
        *green = 255;
        *blue = 255; /* white: FPGA reconfiguration recovery signature */
    } else if (!diagnostics.usb_present) {
        resting_color(red, green, blue);
    } else if (!diagnostics.health.connected) {
        *red = 0;
        *green = 0;
        *blue = 255; /* blue: host has not asserted DTR */
    } else if (diagnostics.health.suspended || stalled_since(now_ms, diagnostics.last_frame_ms)) {
        *red = 255;
        *green = 0;
        *blue = 255; /* magenta: bus suspended or start-of-frame counter frozen */
    } else if (stalled_since(now_ms, diagnostics.last_activity_ms)) {
        *red = 255;
        *green = 0;
        *blue = 0; /* red: connected but no CDC transfer completed recently */
    } else {
        *red = 0;
        *green = 255;
        *blue = 0; /* green: connected and transfers are completing */
    }
}

static void apply_led(uint32_t now_ms) {
    if (diagnostics.led_on) {
        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;
        heartbeat_color(now_ms, &red, &green, &blue);
        bsp_led_set(red, green, blue, HEARTBEAT_BRIGHTNESS);
        diagnostics.commanded = (bsp_led_state_t){
            .red = red,
            .green = green,
            .blue = blue,
            .brightness = HEARTBEAT_BRIGHTNESS,
            .enabled = true,
        };
    } else {
        bsp_led_off();
        diagnostics.commanded.enabled = false;
    }
}

static bool led_readback_matches(void) {
    bsp_led_state_t led = bsp_led_get();
    return led.red == diagnostics.commanded.red && led.green == diagnostics.commanded.green &&
           led.blue == diagnostics.commanded.blue &&
           led.brightness == diagnostics.commanded.brightness &&
           led.enabled == diagnostics.commanded.enabled;
}

/* Runs immediately after the heartbeat LED write, so the readback measures the
   FPGA bus rather than whatever a `color` command left behind between polls. */
static void check_fpga(uint32_t now_ms) {
    bsp_watchdog_marker_set(APPLICATION_DIAGNOSTICS_MARKER_FPGA_CHECK);

    if (bsp_fpga_cdone() && bsp_fpga_ping() == BSP_FPGA_DESIGN_ID && led_readback_matches()) {
        return;
    }

    ++diagnostics.fpga_failures;
    if (bsp_fpga_reconfigure()) {
        ++diagnostics.fpga_reconfigures;
        diagnostics.recovery_toggles = RECOVERY_TOGGLES;
        /* The fresh configuration comes up with its registers cleared, so the
           commanded heartbeat state has to be written again. */
        apply_led(now_ms);
    }
}

static void sample_usb(uint32_t now_ms) {
    bsp_watchdog_marker_set(APPLICATION_DIAGNOSTICS_MARKER_USB_SNAPSHOT);
    diagnostics.health = bsp_usb_health();

    if (diagnostics.health.activity_count != diagnostics.last_activity_count) {
        diagnostics.last_activity_count = diagnostics.health.activity_count;
        diagnostics.last_activity_ms = now_ms;
    }
    if (diagnostics.health.frame_number != diagnostics.last_frame_number) {
        diagnostics.last_frame_number = diagnostics.health.frame_number;
        diagnostics.last_frame_ms = now_ms;
    }
}

static uint32_t packed_health(void) {
    uint32_t packed = diagnostics.health.frame_number & HEALTH_FRAME_MASK;
    packed |= (uint32_t)diagnostics.health.connected << HEALTH_CONNECTED_SHIFT;
    packed |= (uint32_t)diagnostics.health.suspended << HEALTH_SUSPENDED_SHIFT;
    packed |= (uint32_t)(diagnostics.health.write_available == 0) << HEALTH_WRITE_BLOCKED_SHIFT;
    packed |= (diagnostics.fpga_failures & HEALTH_FPGA_FAILURE_MASK) << HEALTH_FPGA_FAILURE_SHIFT;
    packed |= (diagnostics.fpga_reconfigures & HEALTH_FPGA_RECONFIGURE_MASK)
              << HEALTH_FPGA_RECONFIGURE_SHIFT;
    return packed;
}

static void store_snapshots(void) {
    bsp_watchdog_snapshot_set(0, diagnostics.uptime_seconds);
    bsp_watchdog_snapshot_set(1, diagnostics.health.activity_count);
    bsp_watchdog_snapshot_set(2, packed_health());
}

static const char *boot_reason_name(void) {
    switch (diagnostics.boot_reason) {
    case BSP_BOOT_WATCHDOG:
        return "watchdog";
    case BSP_BOOT_BROWNOUT:
        return "brownout";
    case BSP_BOOT_POWER_ON:
        return "power-on";
    default:
        return "other";
    }
}

static void print_boot_report(void) {
    bsp_console_printf("diag: boot=%s marker=%lu loop=%lu usb=%lu health=%08lX\n",
                       boot_reason_name(), (unsigned long)diagnostics.boot_marker,
                       (unsigned long)diagnostics.boot_snapshot[0],
                       (unsigned long)diagnostics.boot_snapshot[1],
                       (unsigned long)diagnostics.boot_snapshot[2]);
}

static uint32_t clamp_blinks(uint32_t marker) {
    if (marker == 0) {
        return 1;
    }
    if (marker > BOOT_BLINK_MAX) {
        return BOOT_BLINK_MAX;
    }
    return marker;
}

static boot_signature_t boot_signature(void) {
    boot_signature_t signature = {255, 255, 255, 1}; /* power-on: one white blink */

    switch (diagnostics.boot_reason) {
    case BSP_BOOT_WATCHDOG:
        /* red, blinked as many times as the retained progress marker */
        signature = (boot_signature_t){255, 0, 0, clamp_blinks(diagnostics.boot_marker)};
        break;
    case BSP_BOOT_BROWNOUT:
        signature = (boot_signature_t){255, 255, 0, 2}; /* yellow */
        break;
    case BSP_BOOT_OTHER:
        signature = (boot_signature_t){0, 255, 255, 3}; /* cyan */
        break;
    default:
        break;
    }
    return signature;
}

/* The USB-free image has no console, so the same boot report is emitted as an
   LED blink code. This runs before the watchdog is armed, so blocking is safe. */
static void blink_boot_report(void) {
    boot_signature_t signature = boot_signature();

    bsp_led_off();
    bsp_time_sleep_ms(BOOT_BLINK_GAP_MS);
    for (uint32_t pass = 0; pass < BOOT_REPORT_REPEATS; ++pass) {
        for (uint32_t blink = 0; blink < signature.blinks; ++blink) {
            bsp_led_set(signature.red, signature.green, signature.blue, HEARTBEAT_BRIGHTNESS);
            bsp_time_sleep_ms(BOOT_BLINK_ON_MS);
            bsp_led_off();
            bsp_time_sleep_ms(BOOT_BLINK_OFF_MS);
        }
        bsp_time_sleep_ms(BOOT_BLINK_GAP_MS);
    }
}

void application_diagnostics_start(void) {
    diagnostics = (diagnostics_state_t){0};
    diagnostics.usb_present = bsp_usb_present();
    diagnostics.boot_reason = bsp_watchdog_boot_reason();
    diagnostics.boot_marker = bsp_watchdog_marker_get();
    for (uint32_t slot = 0; slot < BSP_WATCHDOG_SNAPSHOT_SLOTS; ++slot) {
        diagnostics.boot_snapshot[slot] = bsp_watchdog_snapshot_get(slot);
        bsp_watchdog_snapshot_set(slot, 0);
    }

    if (diagnostics.usb_present) {
        print_boot_report();
    } else {
        blink_boot_report();
    }

    uint32_t now_ms = bsp_time_now_ms();
    diagnostics.led_on = true;
    diagnostics.next_led_ms = now_ms + APPLICATION_DIAGNOSTICS_LED_HALF_PERIOD_MS;
    diagnostics.next_sample_ms = now_ms + APPLICATION_DIAGNOSTICS_SAMPLE_PERIOD_MS;
    diagnostics.last_activity_ms = now_ms;
    diagnostics.last_frame_ms = now_ms;
    apply_led(now_ms);

    bsp_watchdog_marker_set(APPLICATION_DIAGNOSTICS_MARKER_LOOP);
    bsp_watchdog_start(APPLICATION_DIAGNOSTICS_WATCHDOG_TIMEOUT_MS);
}

void application_diagnostics_poll(void) {
    bsp_watchdog_feed();
    bsp_watchdog_marker_set(APPLICATION_DIAGNOSTICS_MARKER_LOOP);

    uint32_t now_ms = bsp_time_now_ms();
    bool led_due = deadline_reached(now_ms, diagnostics.next_led_ms);
    bool sample_due = deadline_reached(now_ms, diagnostics.next_sample_ms);

    /* Sampling first means the heartbeat color below reflects the health just
       read, and the single LED write is the one the FPGA check reads back. */
    if (sample_due) {
        diagnostics.next_sample_ms = now_ms + APPLICATION_DIAGNOSTICS_SAMPLE_PERIOD_MS;
        ++diagnostics.uptime_seconds;
        sample_usb(now_ms);
    }
    if (led_due) {
        diagnostics.next_led_ms = now_ms + APPLICATION_DIAGNOSTICS_LED_HALF_PERIOD_MS;
        diagnostics.led_on = !diagnostics.led_on;
    }
    if (led_due || sample_due) {
        apply_led(now_ms);
    }
    if (led_due && diagnostics.recovery_toggles) {
        --diagnostics.recovery_toggles;
    }
    if (sample_due) {
        check_fpga(now_ms);
        store_snapshots();
        bsp_watchdog_marker_set(APPLICATION_DIAGNOSTICS_MARKER_LOOP);
    }
}

void application_diagnostics_print_report(void) {
    print_boot_report();
    bsp_console_printf(
        "diag: uptime=%lus connected=%u suspended=%u write=%lu activity=%lu sof=%lu "
        "fpga_fail=%lu fpga_reconfig=%lu\n",
        (unsigned long)diagnostics.uptime_seconds, diagnostics.health.connected,
        diagnostics.health.suspended, (unsigned long)diagnostics.health.write_available,
        (unsigned long)diagnostics.health.activity_count,
        (unsigned long)diagnostics.health.frame_number,
        (unsigned long)diagnostics.fpga_failures, (unsigned long)diagnostics.fpga_reconfigures);
}
