#include "application_ibit.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "application_diagnostics.h"
#include "bsp.h"

enum {
    DETAIL_CAPACITY = 112,
    LED_PHASE_MS = 200,
    BUTTON_POLL_MS = 50,
    /* Start-of-frame advances every millisecond while the host is framing, so
       twenty of them is already decisive without being a visible pause. */
    USB_SAMPLE_MS = 20,
    TEMPERATURE_MIN_MILLI_C = -20000,
    TEMPERATURE_MAX_MILLI_C = 85000,
    EXPECTED_SYS_HZ = 150000000,
    EXPECTED_USB_HZ = 48000000,
    /* The frequency counter is gated for a finite window, so its answer carries
       a little quantisation. One percent is far tighter than any real fault and
       far looser than the measurement noise. */
    CLOCK_TOLERANCE_DIVISOR = 100,
    EXPECTED_FLASH_BYTES = 2 * 1024 * 1024,
    EXPECTED_SRAM_BYTES = 520 * 1024,
};

typedef application_ibit_outcome_t (*ibit_run_fn)(char *detail, size_t capacity);

typedef struct {
    const char *name;
    /* Whether the step is meaningless once the FPGA is unreachable. Carried here
       rather than asked inside each step, because a step can span many passes
       and asking per pass would put a bus transaction in the foreground loop
       once a millisecond to re-answer a question that cannot change mid-step. */
    bool needs_fpga;
    ibit_run_fn run;
} ibit_step_t;

typedef struct {
    uint32_t index;
    uint32_t first_index;
    uint32_t last_index;
    bool skipping;
    uint32_t phase;
    uint32_t step_started_ms;
    uint32_t sequence_started_ms;
    uint32_t next_poll_ms;
    uint32_t current_time_ms;
    uint32_t pass;
    uint32_t fail;
    uint32_t timeout;
    uint32_t skip;
    uint32_t info;
    /* BSP_MemoryCheck writes a pattern across the whole PSRAM range, so it is
       sampled once per run and read twice rather than run twice. */
    bool memory_sampled;
    bsp_memory_report_t memory;
    /* Captured before the LED step drives anything, so whatever the user had
       showing comes back afterwards. */
    bool led_saved;
    bsp_led_state_t led_before;
    uint32_t usb_frame_before;
    uint8_t button_count_before;
    uint8_t button_level_before;
    bool button_level_moved;
    uint32_t soak_iterations;
    uint32_t soak_failures;
    uint32_t soak_timeouts;
} ibit_state_t;

static ibit_state_t ibit;

static application_ibit_outcome_t step_chip_identity(char *detail, size_t capacity);
static application_ibit_outcome_t step_board_identity(char *detail, size_t capacity);
static application_ibit_outcome_t step_clocks(char *detail, size_t capacity);
static application_ibit_outcome_t step_memory_sizing(char *detail, size_t capacity);
static application_ibit_outcome_t step_otp_devinfo(char *detail, size_t capacity);
static application_ibit_outcome_t step_boot_flash(char *detail, size_t capacity);
static application_ibit_outcome_t step_psram(char *detail, size_t capacity);
static application_ibit_outcome_t step_temperature(char *detail, size_t capacity);
static application_ibit_outcome_t step_usb(char *detail, size_t capacity);
static application_ibit_outcome_t step_watchdog(char *detail, size_t capacity);
static application_ibit_outcome_t step_fpga_configuration(char *detail, size_t capacity);
static application_ibit_outcome_t step_fpga_registers(char *detail, size_t capacity);
static application_ibit_outcome_t step_led(char *detail, size_t capacity);
static application_ibit_outcome_t step_button(char *detail, size_t capacity);

static const ibit_step_t STEPS[] = {
    {"Chip identity", false, step_chip_identity},
    {"Board identity", false, step_board_identity},
    {"Clocks", false, step_clocks},
    {"Memory sizing", false, step_memory_sizing},
    {"OTP flash device info", false, step_otp_devinfo},
    {"Boot flash", false, step_boot_flash},
    {"QSPI PSRAM", false, step_psram},
    {"Die temperature", false, step_temperature},
    {"USB link", false, step_usb},
    {"Watchdog and boot reason", false, step_watchdog},
    /* Not marked: this is the step that decides whether the FPGA is reachable,
       so skipping it on the grounds that the FPGA is unreachable would remove
       the only report of the fault. */
    {"FPGA configuration", false, step_fpga_configuration},
    {"FPGA register bus", true, step_fpga_registers},
    {"RGB LED", true, step_led},
    {"Button SW1", true, step_button},
};

static const char *const OUTCOME_TEXT[] = {
    "PENDING", "PASS", "FAIL", "TIMEOUT", "SKIP", "INFO",
};

static void mark_write(void) {
    BSP_WatchdogMarkerSet(APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE);
}

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static uint32_t step_elapsed_ms(void) {
    return ibit.current_time_ms - ibit.step_started_ms;
}

/* Samples both QSPI devices at most once per run. They share SCLK and SD0..SD3,
   so one report covers both and a second pass would only add bus traffic. */
static bsp_memory_report_t memory_report(void) {
    if (!ibit.memory_sampled) {
        ibit.memory = BSP_MemoryCheck();
        ibit.memory_sampled = true;
    }
    return ibit.memory;
}

static bool within_tolerance(uint32_t measured, uint32_t expected) {
    const uint32_t allowed = expected / CLOCK_TOLERANCE_DIVISOR;
    const uint32_t difference = measured > expected ? measured - expected : expected - measured;
    return difference <= allowed;
}

static application_ibit_outcome_t verdict(bool ok) {
    return ok ? APPLICATION_IBIT_PASS : APPLICATION_IBIT_FAIL;
}

/* Asked of the FPGA itself, every time, rather than read from BSP_FpgaIsReady.
   That flag records what bring-up found and is only rewritten by a
   reconfiguration, so an FPGA that died after boot still reports ready and the
   steps that sit behind it would run and produce failures of their own instead
   of standing down. Three extra pings across a sequence is a cheap price for a
   skip decision made on this run's evidence. */
static bool fpga_reachable(void) {
    return BSP_FpgaCdone() && BSP_FpgaPing() == BSP_FPGA_DESIGN_ID;
}





/***** the steps *****/


static application_ibit_outcome_t step_chip_identity(char *detail, size_t capacity) {
    const bsp_mcu_info_t info = BSP_McuInfo();
    const bool ok = info.manufacturer == BSP_MCU_MANUFACTURER_RASPBERRY_PI &&
                    info.part == BSP_MCU_PART_RP2350;

    snprintf(detail, capacity, "manufacturer=%03X part=%04X revision=%u %s x%u",
             info.manufacturer, info.part, info.revision,
             info.architecture == BSP_MCU_ARCHITECTURE_ARM ? "Arm" : "RISC-V",
             info.core_count);
    return verdict(ok);
}


/* All-zero and all-ones are what a bus that answered with nothing looks like, so
   both are rejected even though either is a legal-looking number. */
static application_ibit_outcome_t step_board_identity(char *detail, size_t capacity) {
    const bsp_mcu_info_t info = BSP_McuInfo();
    uint32_t zeroes = 0;
    uint32_t ones = 0;

    for (uint32_t index = 0; index < BSP_MCU_UNIQUE_ID_BYTES; ++index) {
        zeroes += info.unique_id[index] == 0x00u;
        ones += info.unique_id[index] == 0xffu;
    }

    snprintf(detail, capacity, "%02X%02X%02X%02X%02X%02X%02X%02X", info.unique_id[0],
             info.unique_id[1], info.unique_id[2], info.unique_id[3], info.unique_id[4],
             info.unique_id[5], info.unique_id[6], info.unique_id[7]);
    return verdict(zeroes != BSP_MCU_UNIQUE_ID_BYTES && ones != BSP_MCU_UNIQUE_ID_BYTES);
}


/* Measured, not configured. clock_get_hz reports what the SDK asked for, so a
   PLL that never locked still reads correct there and only shows up here. */
static application_ibit_outcome_t step_clocks(char *detail, size_t capacity) {
    const bsp_clocks_report_t clocks = BSP_ClocksReport();
    const bool sys_ok = within_tolerance(clocks.measured_sys_hz, EXPECTED_SYS_HZ);
    const bool usb_ok = within_tolerance(clocks.measured_usb_hz, EXPECTED_USB_HZ);
    /* RP2350-E12 wants clk_sys at least ten percent above clk_usb, and there is
       deliberately no separate verdict for it, because there cannot be a failing
       one. A measured clk_sys within one percent of 150 MHz is by construction
       more than ten percent above a measured clk_usb within one percent of
       48 MHz, so a test for the margin would be a branch nothing can take. It
       was previously read off the configured values, where it could fail --
       which only meant it was answering a different question: whether the SDK
       intended a legal ratio, which it always did.

       The ratio is printed instead so the margin stays visible. If the expected
       frequencies ever stop being pinned to 150 and 48, this has to go back to
       being a real check. */
    const uint32_t ratio_hundredths =
        clocks.measured_usb_hz == 0u
            ? 0u
            : (uint32_t)(((uint64_t)clocks.measured_sys_hz * 100u) / clocks.measured_usb_hz);

    snprintf(detail, capacity, "sys=%lu.%03luMHz usb=%lu.%03luMHz ref=%luHz sys/usb=%lu.%02lux",
             (unsigned long)(clocks.measured_sys_hz / 1000000u),
             (unsigned long)((clocks.measured_sys_hz / 1000u) % 1000u),
             (unsigned long)(clocks.measured_usb_hz / 1000000u),
             (unsigned long)((clocks.measured_usb_hz / 1000u) % 1000u),
             (unsigned long)clocks.ref_hz, (unsigned long)(ratio_hundredths / 100u),
             (unsigned long)(ratio_hundredths % 100u));
    return verdict(sys_ok && usb_ok);
}


static application_ibit_outcome_t step_memory_sizing(char *detail, size_t capacity) {
    const bsp_mcu_info_t info = BSP_McuInfo();

    snprintf(detail, capacity, "flash=%luKiB sram=%luKiB",
             (unsigned long)(info.flash_bytes / 1024u),
             (unsigned long)(info.sram_bytes / 1024u));
    return verdict(info.flash_bytes == EXPECTED_FLASH_BYTES &&
                   info.sram_bytes == EXPECTED_SRAM_BYTES);
}


/* Reported, not judged. Measured on hardware this part answers 0x9 for chip
   select 0, which is the correct 2 MByte, and 0x0 for chip select 1 even though
   a 2 MByte device is fitted and working there. So one of the two is right and
   the other is not, and there is no way to tell them apart from the numbers
   alone -- which is exactly why this prints them and stops. Nothing in the
   firmware sizes a memory from here; flash comes from what the image was linked
   for and the DRAM from the SDK's own detection. */
static application_ibit_outcome_t step_otp_devinfo(char *detail, size_t capacity) {
    const bsp_mcu_info_t info = BSP_McuInfo();

    snprintf(detail, capacity, "cs0=0x%X cs1=0x%X (reported, not used to size anything)",
             info.otp_cs0_size_code, info.otp_cs1_size_code);
    return APPLICATION_IBIT_INFO;
}


static application_ibit_outcome_t step_boot_flash(char *detail, size_t capacity) {
    const bsp_memory_report_t memory = memory_report();

    snprintf(detail, capacity, "%luKiB, reset vector %s",
             (unsigned long)(memory.flash_bytes / 1024u), memory.flash_ok ? "sane" : "BAD");
    return verdict(memory.flash_ok);
}


/* The identity mismatch is reported and not failed. Identity and function are
   separate questions: this device passes a pattern across its whole range but
   calls itself KGD 0x0B EID 0x43 rather than AP Memory's 0x5D, so the fitted
   part is not the APS1604M-3SQR-SN on the schematic. Reading the package marking
   is what would settle it, and no test can. */
static application_ibit_outcome_t step_psram(char *detail, size_t capacity) {
    const bsp_memory_report_t memory = memory_report();

    /* FORGIX_QSPI_PSRAM is a supported way to build this firmware, and with it
       off the device is never brought up. Reporting that as a failure would
       accuse a board of a fault that is really a build decision -- and the two
       are indistinguishable from the numbers, since a device that was never
       enabled and one that failed detection both read zero bytes and not ok. */
    if (!memory.psram_enabled) {
        snprintf(detail, capacity, "not enabled in this build (FORGIX_QSPI_PSRAM off)");
        return APPLICATION_IBIT_SKIP;
    }

    snprintf(detail, capacity, "%luKiB pattern %s, kgd=%02X eid=%02X%s",
             (unsigned long)(memory.psram_bytes / 1024u), memory.psram_ok ? "held" : "LOST",
             memory.psram_kgd, memory.psram_eid,
             memory.psram_kgd == 0x5du ? "" : " (not the part on the schematic)");
    return verdict(memory.psram_ok);
}


/* Banded, not compared. The uncalibrated sensor is several degrees out on a good
   day, so an exact figure would be a lie; a reading pinned at either rail is the
   fault worth catching, and that a band finds. */
static application_ibit_outcome_t step_temperature(char *detail, size_t capacity) {
    const bsp_adc_temperature_t sample = BSP_AdcTemperature();
    const bool ok = sample.milli_celsius >= TEMPERATURE_MIN_MILLI_C &&
                    sample.milli_celsius <= TEMPERATURE_MAX_MILLI_C;

    /* The sign is carried separately rather than left to the integer division.
       Truncation toward zero loses it for anything between -1 C and 0 C, where
       -0.5 would have printed as "0.5C" -- a wrong reading rather than an
       imprecise one, and on the only part of the scale where the reader most
       needs to know which side of freezing the board is on. */
    const int32_t magnitude =
        sample.milli_celsius < 0 ? -sample.milli_celsius : sample.milli_celsius;

    snprintf(detail, capacity, "%s%ld.%01ldC raw=%u", sample.milli_celsius < 0 ? "-" : "",
             (long)(magnitude / 1000), (long)((magnitude / 100) % 10), sample.raw);
    return verdict(ok);
}


/* Two samples, because a single frame number proves nothing. The host's
   start-of-frame counter advancing is the only evidence available that the bus
   is live rather than merely enumerated. */
static application_ibit_outcome_t step_usb(char *detail, size_t capacity) {
    const bsp_usb_health_t health = BSP_UsbHealth();

    if (ibit.phase == 0) {
        ibit.usb_frame_before = health.frame_number;
        ibit.phase = 1;
        return APPLICATION_IBIT_PENDING;
    }
    if (step_elapsed_ms() < USB_SAMPLE_MS) {
        return APPLICATION_IBIT_PENDING;
    }

    const bool framing = health.frame_number != ibit.usb_frame_before;
    snprintf(detail, capacity, "dtr=%u suspended=%u sof %s txfree=%lu", health.connected,
             health.suspended, framing ? "advancing" : "FROZEN",
             (unsigned long)health.write_available);
    return verdict(health.connected && !health.suspended && framing &&
                   health.write_available > 0u);
}


/* A previous watchdog reset is a failure even though the board is plainly
   running now: it means something stopped feeding the loop, and the retained
   marker is the only witness to where.

   The reason comes from the diagnostics layer's boot-time snapshot rather than
   from a fresh BSP_WatchdogBootReason call. Arming the watchdog writes the
   scratch word watchdog_enable_caused_reboot consults, so asking again once the
   foreground loop is running reports a watchdog reset on every board -- which is
   how this step first failed on hardware that had powered up perfectly. */
static application_ibit_outcome_t step_watchdog(char *detail, size_t capacity) {
    const bsp_boot_reason reason = application_diagnostics_boot_reason();
    static const char *const REASON_TEXT[] = {"power-on", "brownout", "watchdog", "other"};

    /* A pattern, not the marker the runner already wrote a few lines earlier. A
       register stuck at APPLICATION_DIAGNOSTICS_MARKER_IBIT would have passed a
       round trip that wrote the value it was already stuck at, which tests
       nothing. Restored immediately afterwards so a reset during the rest of
       this step still attributes itself to the built-in test. */
    BSP_WatchdogMarkerSet(APPLICATION_DIAGNOSTICS_MARKER_SELF_TEST_PATTERN);
    const bool marker_ok =
        BSP_WatchdogMarkerGet() == APPLICATION_DIAGNOSTICS_MARKER_SELF_TEST_PATTERN;
    BSP_WatchdogMarkerSet(APPLICATION_DIAGNOSTICS_MARKER_IBIT);

    snprintf(detail, capacity, "last boot %s, marker readback %s", REASON_TEXT[reason],
             marker_ok ? "ok" : "BAD");
    return verdict(marker_ok && reason != BSP_BOOT_WATCHDOG);
}


/* Also the only proof the 32 MHz oscillator and its GPIO 19 gate are alive. A
   design with no clock does not answer a ping at all, so a correct design ID
   here has already cleared them both. */
static application_ibit_outcome_t step_fpga_configuration(char *detail, size_t capacity) {
    const bool cdone = BSP_FpgaCdone();
    const uint8_t id = BSP_FpgaPing();

    snprintf(detail, capacity, "cdone=%u id=%02X status_pin=%u (32MHz oscillator implied)",
             cdone, id, BSP_FpgaStatusPin());
    return verdict(cdone && id == BSP_FPGA_DESIGN_ID);
}


static application_ibit_outcome_t step_fpga_registers(char *detail, size_t capacity) {
    const uint8_t status = BSP_FpgaReadStatus();
    const bsp_led_state_t before = BSP_LedGet();

    /* A walking pattern rather than a constant: 0x00 and 0xFF are what a bus
       stuck low or high returns, and either would pass a test that wrote them. */
    BSP_LedSet(0x5au, 0xa5u, 0x3cu, 0xc3u);
    const bsp_led_state_t readback = BSP_LedGet();
    BSP_LedSet(before.red, before.green, before.blue, before.brightness);

    const bool ok = readback.red == 0x5au && readback.green == 0xa5u &&
                    readback.blue == 0x3cu && readback.brightness == 0xc3u;
    snprintf(detail, capacity, "status=%02X wrote 5A,A5,3C,C3 read %02X,%02X,%02X,%02X", status,
             readback.red, readback.green, readback.blue, readback.brightness);
    return verdict(ok);
}


/* Drives each channel on its own so a user watching can see which one is dead,
   and reads every one back so an unattended run still produces a verdict. */
static application_ibit_outcome_t step_led(char *detail, size_t capacity) {
    static const uint8_t COLOURS[][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0},
    };
    static const uint32_t COLOUR_COUNT = sizeof COLOURS / sizeof COLOURS[0];

    if (!ibit.led_saved) {
        ibit.led_before = BSP_LedGet();
        ibit.led_saved = true;
        ibit.phase = 0;
    }

    const uint32_t due = step_elapsed_ms() / LED_PHASE_MS;
    if (due < ibit.phase) {
        return APPLICATION_IBIT_PENDING;
    }
    if (ibit.phase < COLOUR_COUNT) {
        const uint8_t *colour = COLOURS[ibit.phase];
        BSP_LedSet(colour[0], colour[1], colour[2], 128);
        const bsp_led_state_t readback = BSP_LedGet();
        if (readback.red != colour[0] || readback.green != colour[1] ||
                readback.blue != colour[2]) {
            snprintf(detail, capacity, "readback mismatch at step %lu: %u,%u,%u",
                     (unsigned long)ibit.phase, readback.red, readback.green, readback.blue);
            BSP_LedSet(ibit.led_before.red, ibit.led_before.green, ibit.led_before.blue,
                       ibit.led_before.brightness);
            return APPLICATION_IBIT_FAIL;
        }
        ++ibit.phase;
        return APPLICATION_IBIT_PENDING;
    }

    BSP_LedSet(ibit.led_before.red, ibit.led_before.green, ibit.led_before.blue,
               ibit.led_before.brightness);
    snprintf(detail, capacity, "red, green, blue and white all read back; previous colour restored");
    return APPLICATION_IBIT_PASS;
}


/* Both halves have to move. The count alone could be a stuck event line and the
   level alone could be a pin held low, so requiring the pair is what separates a
   real press from a fault that looks like one. */
static application_ibit_outcome_t step_button(char *detail, size_t capacity) {
    if (ibit.phase == 0) {
        /* Cleared before the baseline is taken. The FPGA's counter is eight bits
           and saturates rather than wrapping, so on a board anyone has been
           pressing since power-up it eventually sits at 255 and never changes
           again -- and a test waiting for it to change could never pass on
           exactly the boards that have seen the most use. */
        BSP_ButtonClearCount();
        const bsp_button_state_t start = BSP_ButtonGetState();
        ibit.button_count_before = start.count;
        ibit.button_level_before = start.level;
        ibit.button_level_moved = false;
        ibit.phase = 1;
        ibit.next_poll_ms = ibit.current_time_ms;
        mark_write();
        BSP_ConsolePrintf("        press SW1 within %lus ...\n",
                           (unsigned long)(APPLICATION_IBIT_BUTTON_TIMEOUT_MS / 1000u));
        return APPLICATION_IBIT_PENDING;
    }
    if (!deadline_reached(ibit.current_time_ms, ibit.next_poll_ms)) {
        return APPLICATION_IBIT_PENDING;
    }

    ibit.next_poll_ms = ibit.current_time_ms + BUTTON_POLL_MS;
    const bsp_button_state_t now = BSP_ButtonGetState();
    ibit.button_level_moved =
        ibit.button_level_moved || (now.level != ibit.button_level_before);

    /* The count alone decides it. The FPGA debounces and counts edges
       continuously; the level is a 50 ms sample of a line a person holds down
       for maybe a tenth of a second, so a brisk tap increments the count and is
       back at rest before the level is next read. Requiring both threw those
       presses away and reported a working button as a timeout.

       The level is still watched, and still reported, because it is the thing
       that says whether the pin moves as well as whether the counter does -- but
       reporting is all it can honestly support at this sample rate. A counter
       running free without any press shows up in how far it moved, which is why
       the count is printed rather than merely tested. */
    if (now.count != ibit.button_count_before) {
        snprintf(detail, capacity, "pressed after %lu.%lus, count %u -> %u, level %s",
                 (unsigned long)(step_elapsed_ms() / 1000u),
                 (unsigned long)((step_elapsed_ms() / 100u) % 10u), ibit.button_count_before,
                 now.count, ibit.button_level_moved ? "seen to move" : "never sampled moving");
        return APPLICATION_IBIT_PASS;
    }
    if (step_elapsed_ms() < APPLICATION_IBIT_BUTTON_TIMEOUT_MS) {
        return APPLICATION_IBIT_PENDING;
    }

    snprintf(detail, capacity, "no press within %lus; count stayed at %u",
             (unsigned long)(APPLICATION_IBIT_BUTTON_TIMEOUT_MS / 1000u),
             ibit.button_count_before);
    return APPLICATION_IBIT_TIMEOUT;
}




/***** the runner *****/


/* Left-aligned to a fixed column rather than dot-leadered. Dots read better, but
   every way of drawing them needs a "name too long" branch that no step name can
   currently reach, and an unreachable branch is a hole in the coverage gate that
   would have to be argued away rather than tested. */
static void print_result(uint32_t index, application_ibit_outcome_t outcome,
                         const char *detail) {
    mark_write();
    BSP_ConsolePrintf("[%2lu/%2lu] %-26s %-7s  %s\n", (unsigned long)(index + 1u),
                       (unsigned long)application_ibit_step_count(), STEPS[index].name,
                       OUTCOME_TEXT[outcome], detail);
}

static void tally(application_ibit_outcome_t outcome) {
    if (outcome == APPLICATION_IBIT_PASS) {
        ++ibit.pass;
    } else if (outcome == APPLICATION_IBIT_FAIL) {
        ++ibit.fail;
    } else if (outcome == APPLICATION_IBIT_TIMEOUT) {
        ++ibit.timeout;
    } else if (outcome == APPLICATION_IBIT_SKIP) {
        ++ibit.skip;
    } else {
        ++ibit.info;
    }
}

/* The dependency is settled once, here, rather than inside the steps. A step can
   span many passes, and asking per pass would put an FPGA transaction in the
   foreground loop once a millisecond to re-answer a question that cannot change
   while the step is running. */
static void begin_step(uint32_t index) {
    ibit.index = index;
    ibit.phase = 0;
    ibit.led_saved = false;
    ibit.step_started_ms = ibit.current_time_ms;
    ibit.skipping = STEPS[index].needs_fpga && !fpga_reachable();
}

static void begin_run(uint32_t first_index, uint32_t last_index) {
    ibit.current_time_ms = BSP_TimeNowMs();
    ibit.first_index = first_index;
    ibit.last_index = last_index;
    ibit.sequence_started_ms = ibit.current_time_ms;
    ibit.pass = 0;
    ibit.fail = 0;
    ibit.timeout = 0;
    ibit.skip = 0;
    ibit.info = 0;
    ibit.memory_sampled = false;
    begin_step(first_index);
}

static void print_summary(void) {
    const uint32_t elapsed_ms = ibit.current_time_ms - ibit.sequence_started_ms;

    mark_write();
    BSP_ConsolePrintf("\nIBIT: %lu PASS  %lu FAIL  %lu TIMEOUT  %lu SKIP  %lu INFO  in %lu.%lus\n",
                       (unsigned long)ibit.pass, (unsigned long)ibit.fail,
                       (unsigned long)ibit.timeout, (unsigned long)ibit.skip,
                       (unsigned long)ibit.info, (unsigned long)(elapsed_ms / 1000u),
                       (unsigned long)((elapsed_ms / 100u) % 10u));
}

/* One step per pass at most, so the foreground loop keeps feeding the watchdog
   whatever any individual step is waiting for. */
static bool advance(void) {
    char detail[DETAIL_CAPACITY] = {0};

    BSP_WatchdogMarkerSet(APPLICATION_DIAGNOSTICS_MARKER_IBIT);
    ibit.current_time_ms = BSP_TimeNowMs();

    application_ibit_outcome_t outcome = APPLICATION_IBIT_SKIP;
    if (ibit.skipping) {
        snprintf(detail, sizeof detail, "FPGA not responding; this test sits behind it");
    } else {
        outcome = STEPS[ibit.index].run(detail, sizeof detail);
    }
    if (outcome == APPLICATION_IBIT_PENDING) {
        return true;
    }

    tally(outcome);
    print_result(ibit.index, outcome, detail);
    if (ibit.index == ibit.last_index) {
        return false;
    }

    begin_step(ibit.index + 1u);
    return true;
}

/* Anything left mid-run is put back here, because an abort is exactly when the
   LED is most likely to be sitting on a test colour. */
static void restore(void) {
    if (ibit.led_saved) {
        BSP_LedSet(ibit.led_before.red, ibit.led_before.green, ibit.led_before.blue,
                   ibit.led_before.brightness);
        ibit.led_saved = false;
    }
}

static void sequence_start(void) {
    mark_write();
    BSP_ConsolePrintf("\nInitiated built-in test\n\n");
    begin_run(0, application_ibit_step_count() - 1u);
}

static bool sequence_poll(void) {
    if (advance()) {
        return true;
    }
    print_summary();
    return false;
}

static void soak_start(void) {
    ibit.soak_iterations = 0;
    ibit.soak_failures = 0;
    ibit.soak_timeouts = 0;
    mark_write();
    BSP_ConsolePrintf("\nIBIT soak; press any key to stop\n\n");
    begin_run(0, application_ibit_step_count() - 1u);
}

static bool soak_poll(void) {
    if (advance()) {
        return true;
    }

    print_summary();
    ++ibit.soak_iterations;

    /* Failures and timeouts are tallied apart, and only failures are the
       headline. A soak is unattended by definition, so the button times out on
       every iteration; folding that into the failure count would make the one
       number a burn-in exists to produce equal the run count forever. */
    ibit.soak_failures += (ibit.fail > 0u) ? 1u : 0u;
    ibit.soak_timeouts += (ibit.timeout > 0u) ? 1u : 0u;

    mark_write();
    BSP_ConsolePrintf("soak: %lu run(s), %lu with a failure, %lu with a timeout\n\n",
                       (unsigned long)ibit.soak_iterations, (unsigned long)ibit.soak_failures,
                       (unsigned long)ibit.soak_timeouts);

    begin_run(0, application_ibit_step_count() - 1u);
    return true;
}

static void single_start(void) {
    mark_write();
    BSP_ConsolePrintf("\n%s\n\n", STEPS[ibit.index].name);
    begin_run(ibit.index, ibit.index);
}

static bool single_poll(void) {
    return advance();
}

static const application_activity_t SEQUENCE = {
    .name = "IBIT",
    .start = sequence_start,
    .poll = sequence_poll,
    .stop = restore,
};

static const application_activity_t SOAK = {
    .name = "IBIT soak",
    .start = soak_start,
    .poll = soak_poll,
    .stop = restore,
};

static const application_activity_t SINGLE = {
    .name = "IBIT step",
    .start = single_start,
    .poll = single_poll,
    .stop = restore,
};




/***** public interface *****/


uint32_t application_ibit_step_count(void) {
    return (uint32_t)(sizeof STEPS / sizeof STEPS[0]);
}

const char *application_ibit_step_name(uint32_t index) {
    return STEPS[index].name;
}

const application_activity_t *application_ibit_sequence(void) {
    return &SEQUENCE;
}

const application_activity_t *application_ibit_soak(void) {
    return &SOAK;
}

const application_activity_t *application_ibit_single(uint32_t index) {
    ibit.index = index;
    return &SINGLE;
}

/* The same facts the sequence checks, printed without judging them. Useful when
   a board is behaving and the question is what it actually is, rather than
   whether it is well. */
void application_ibit_print_board_report(void) {
    const bsp_mcu_info_t info = BSP_McuInfo();
    const bsp_clocks_report_t clocks = BSP_ClocksReport();
    const bsp_adc_temperature_t temperature = BSP_AdcTemperature();

    mark_write();
    BSP_ConsolePrintf("\nchip     manufacturer=%03X part=%04X revision=%u %s x%u\n",
                       info.manufacturer, info.part, info.revision,
                       info.architecture == BSP_MCU_ARCHITECTURE_ARM ? "Arm" : "RISC-V",
                       info.core_count);
    mark_write();
    BSP_ConsolePrintf("board    %02X%02X%02X%02X%02X%02X%02X%02X\n", info.unique_id[0],
                       info.unique_id[1], info.unique_id[2], info.unique_id[3],
                       info.unique_id[4], info.unique_id[5], info.unique_id[6],
                       info.unique_id[7]);
    mark_write();
    BSP_ConsolePrintf("package  id=%08lX device=%08lX%08lX valid=%u\n",
                       (unsigned long)info.package_id, (unsigned long)info.device_id_high,
                       (unsigned long)info.device_id_low, info.chip_info_valid);
    mark_write();
    BSP_ConsolePrintf("memory   flash=%luKiB sram=%luKiB otp_cs0=0x%X otp_cs1=0x%X\n",
                       (unsigned long)(info.flash_bytes / 1024u),
                       (unsigned long)(info.sram_bytes / 1024u), info.otp_cs0_size_code,
                       info.otp_cs1_size_code);
    mark_write();
    BSP_ConsolePrintf("clocks   sys=%lu usb=%lu ref=%lu peri=%lu adc=%lu Hz configured\n",
                       (unsigned long)clocks.sys_hz, (unsigned long)clocks.usb_hz,
                       (unsigned long)clocks.ref_hz, (unsigned long)clocks.peri_hz,
                       (unsigned long)clocks.adc_hz);
    mark_write();
    BSP_ConsolePrintf("measured sys=%lu usb=%lu Hz\n", (unsigned long)clocks.measured_sys_hz,
                       (unsigned long)clocks.measured_usb_hz);
    mark_write();
    BSP_ConsolePrintf("die      %ld milli-degrees C, raw=%u\n",
                       (long)temperature.milli_celsius, temperature.raw);
}
