#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "application_ibit.h"
#include "mock_bsp_console.h"
#include "mock_bsp_time.h"
#include "mock_bsp_usb.h"
#include "mock_auto_application_diagnostics.h"
#include "mock_bsp_watchdog.h"
#include "mock_auto_bsp_adc.h"
#include "mock_auto_bsp_button.h"
#include "mock_auto_bsp_clocks.h"
#include "mock_auto_bsp_fpga.h"
#include "mock_auto_bsp_led.h"
#include "mock_auto_bsp_mcu.h"
#include "mock_auto_bsp_memory.h"

enum {
    STEP_CHIP_IDENTITY = 0,
    STEP_BOARD_IDENTITY = 1,
    STEP_CLOCKS = 2,
    STEP_MEMORY_SIZING = 3,
    STEP_OTP_DEVINFO = 4,
    STEP_BOOT_FLASH = 5,
    STEP_PSRAM = 6,
    STEP_TEMPERATURE = 7,
    STEP_USB = 8,
    STEP_WATCHDOG = 9,
    STEP_FPGA_CONFIGURATION = 10,
    STEP_FPGA_REGISTERS = 11,
    STEP_LED = 12,
    STEP_BUTTON = 13,
};

void setUp(void) {
    MOCK_BSP_ConsoleReset();
    MOCK_BSP_TimeReset();
    MOCK_BSP_UsbReset();
    MOCK_BSP_WatchdogReset();
}

void tearDown(void) {
}

static bsp_mcu_info_t healthy_mcu(void) {
    bsp_mcu_info_t info = {
        .manufacturer = BSP_MCU_MANUFACTURER_RASPBERRY_PI,
        .part = BSP_MCU_PART_RP2350,
        .revision = 2,
        .package_id = 0x11223344u,
        .device_id_low = 0xaabbccddu,
        .device_id_high = 0x01020304u,
        .chip_info_valid = true,
        .unique_id = {0xe6, 0x60, 0x38, 0xb7, 0x13, 0x5f, 0x21, 0x2c},
        .sram_bytes = 520u * 1024u,
        .flash_bytes = 2u * 1024u * 1024u,
        .otp_cs0_size_code = 0x9u,  /* what the board actually reports: 2 MByte */
        .otp_cs1_size_code = 0u,
        .core_count = 2,
        .architecture = BSP_MCU_ARCHITECTURE_ARM,
    };
    return info;
}

static bsp_clocks_report_t healthy_clocks(void) {
    bsp_clocks_report_t clocks = {
        .sys_hz = 150000000u,
        .usb_hz = 48000000u,
        .ref_hz = 12000000u,
        .peri_hz = 150000000u,
        .adc_hz = 48000000u,
        .measured_sys_hz = 150000000u,
        .measured_usb_hz = 48000000u,
    };
    return clocks;
}

static bsp_memory_report_t healthy_memory(void) {
    bsp_memory_report_t memory = {
        .flash_bytes = 2u * 1024u * 1024u,
        .flash_ok = true,
        .psram_bytes = 2u * 1024u * 1024u,
        .psram_ok = true,
        .psram_forced = false,
        .psram_kgd = 0x0bu,
        .psram_eid = 0x43u,
    };
    return memory;
}

static bsp_usb_health_t usb_health(uint32_t frame, bool connected, bool suspended,
                                   uint32_t write_available) {
    bsp_usb_health_t health = {
        .connected = connected,
        .suspended = suspended,
        .write_available = write_available,
        .activity_count = 1,
        .frame_number = frame,
    };
    return health;
}

/* A named helper rather than a compound literal at the call site: the commas in
   a designated initializer split CMock's expectation macros into extra
   arguments. */
static bsp_adc_temperature_t temperature_sample(uint16_t raw, int32_t milli_celsius) {
    bsp_adc_temperature_t sample = {.raw = raw, .milli_celsius = milli_celsius};
    return sample;
}

static bsp_led_state_t led_state(uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness) {
    bsp_led_state_t led = {
        .red = red, .green = green, .blue = blue, .brightness = brightness, .enabled = true};
    return led;
}

/* Runs one step in isolation and returns everything it printed. Each step is
   driven through application_ibit_single so the runner, the tally and the result
   line are exercised alongside the step itself. */
static const char *run_step_at(uint32_t index, uint32_t now_ms) {
    MOCK_BSP_TimeSetMs(now_ms);
    const application_activity_t *activity = application_ibit_single(index);
    activity->start();
    MOCK_BSP_ConsoleReset();
    while (activity->poll()) {
        MOCK_BSP_TimeSetMs(now_ms);
    }
    return MOCK_BSP_ConsoleOutput();
}

static const char *run_step(uint32_t index) {
    return run_step_at(index, 1000);
}




/***** identity, clocks and memory *****/


void test_chip_identity_passes_on_a_raspberry_pi_rp2350(void) {
    BSP_McuInfo_ExpectAndReturn(healthy_mcu());

    const char *output = run_step(STEP_CHIP_IDENTITY);

    TEST_ASSERT_NOT_NULL(strstr(output, "PASS"));
    TEST_ASSERT_NOT_NULL(strstr(output, "manufacturer=493 part=0004 revision=2 Arm x2"));
}

void test_chip_identity_fails_on_an_unexpected_part(void) {
    bsp_mcu_info_t info = healthy_mcu();
    info.part = 0x0002u;
    info.architecture = BSP_MCU_ARCHITECTURE_RISCV;
    BSP_McuInfo_ExpectAndReturn(info);

    const char *output = run_step(STEP_CHIP_IDENTITY);

    TEST_ASSERT_NOT_NULL(strstr(output, "FAIL"));
    TEST_ASSERT_NOT_NULL(strstr(output, "RISC-V"));
}

void test_chip_identity_fails_on_an_unexpected_manufacturer(void) {
    bsp_mcu_info_t info = healthy_mcu();
    info.manufacturer = 0x0123u;
    BSP_McuInfo_ExpectAndReturn(info);

    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_CHIP_IDENTITY), "FAIL"));
}

void test_board_identity_reports_the_unique_id(void) {
    BSP_McuInfo_ExpectAndReturn(healthy_mcu());

    const char *output = run_step(STEP_BOARD_IDENTITY);

    TEST_ASSERT_NOT_NULL(strstr(output, "PASS"));
    TEST_ASSERT_NOT_NULL(strstr(output, "E66038B7135F212C"));
}

/* All-zero and all-ones are what a bus that answered with nothing looks like. */
void test_board_identity_rejects_an_all_zero_and_an_all_ones_id(void) {
    bsp_mcu_info_t zeroed = healthy_mcu();
    memset(zeroed.unique_id, 0x00, sizeof zeroed.unique_id);
    BSP_McuInfo_ExpectAndReturn(zeroed);
    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_BOARD_IDENTITY), "FAIL"));

    bsp_mcu_info_t ones = healthy_mcu();
    memset(ones.unique_id, 0xff, sizeof ones.unique_id);
    BSP_McuInfo_ExpectAndReturn(ones);
    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_BOARD_IDENTITY), "FAIL"));
}

void test_clocks_pass_when_the_measured_frequencies_match(void) {
    BSP_ClocksReport_ExpectAndReturn(healthy_clocks());

    const char *output = run_step(STEP_CLOCKS);

    TEST_ASSERT_NOT_NULL(strstr(output, "PASS"));
    TEST_ASSERT_NOT_NULL(strstr(output, "sys=150.000MHz usb=48.000MHz"));
    TEST_ASSERT_NOT_NULL(strstr(output, "E12margin=ok"));
}

/* The configured value still reads correct when a PLL never locked, so only the
   measured one can catch it. */
void test_clocks_fail_when_the_measurement_disagrees_with_the_configuration(void) {
    bsp_clocks_report_t clocks = healthy_clocks();
    clocks.measured_sys_hz = 125000000u;
    BSP_ClocksReport_ExpectAndReturn(clocks);

    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_CLOCKS), "FAIL"));

    clocks = healthy_clocks();
    clocks.measured_usb_hz = 12000000u;
    BSP_ClocksReport_ExpectAndReturn(clocks);

    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_CLOCKS), "FAIL"));
}

/* Overspeed matters as much as underspeed: an over-locked PLL is still a PLL
   that is not doing what the SDK believes it is. */
void test_clocks_fail_when_the_measurement_runs_fast(void) {
    bsp_clocks_report_t clocks = healthy_clocks();
    clocks.measured_sys_hz = 200000000u;
    BSP_ClocksReport_ExpectAndReturn(clocks);

    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_CLOCKS), "FAIL"));
}

/* RP2350-E12 wants clk_sys at least ten percent above clk_usb. */
void test_clocks_fail_when_the_errata_margin_is_violated(void) {
    bsp_clocks_report_t clocks = healthy_clocks();
    clocks.sys_hz = 48000000u;
    BSP_ClocksReport_ExpectAndReturn(clocks);

    const char *output = run_step(STEP_CLOCKS);

    TEST_ASSERT_NOT_NULL(strstr(output, "FAIL"));
    TEST_ASSERT_NOT_NULL(strstr(output, "E12margin=VIOLATED"));
}

void test_memory_sizing_checks_flash_and_sram_against_what_the_part_should_have(void) {
    BSP_McuInfo_ExpectAndReturn(healthy_mcu());
    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_MEMORY_SIZING), "flash=2048KiB sram=520KiB"));

    bsp_mcu_info_t info = healthy_mcu();
    info.flash_bytes = 16u * 1024u * 1024u;
    BSP_McuInfo_ExpectAndReturn(info);
    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_MEMORY_SIZING), "FAIL"));

    info = healthy_mcu();
    info.sram_bytes = 264u * 1024u;
    BSP_McuInfo_ExpectAndReturn(info);
    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_MEMORY_SIZING), "FAIL"));
}

/* Reported, never believed: these are unprogrammed OTP defaults on this part. */
void test_otp_device_info_is_reported_without_a_verdict(void) {
    BSP_McuInfo_ExpectAndReturn(healthy_mcu());

    const char *output = run_step(STEP_OTP_DEVINFO);

    TEST_ASSERT_NOT_NULL(strstr(output, "INFO"));
    TEST_ASSERT_NOT_NULL(strstr(output, "cs0=0x9 cs1=0x0"));
    TEST_ASSERT_NOT_NULL(strstr(output, "not used to size anything"));
}

void test_boot_flash_reports_the_reset_vector_verdict(void) {
    BSP_MemoryCheck_ExpectAndReturn(healthy_memory());
    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_BOOT_FLASH), "reset vector sane"));

    bsp_memory_report_t memory = healthy_memory();
    memory.flash_ok = false;
    BSP_MemoryCheck_ExpectAndReturn(memory);
    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_BOOT_FLASH), "FAIL"));
}

/* The fitted part answers KGD 0x0B rather than AP Memory's 0x5D. It works, so
   identity is a note on a passing result and not a failure. */
void test_psram_passes_a_working_device_while_naming_the_identity_mismatch(void) {
    BSP_MemoryCheck_ExpectAndReturn(healthy_memory());

    const char *output = run_step(STEP_PSRAM);

    TEST_ASSERT_NOT_NULL(strstr(output, "PASS"));
    TEST_ASSERT_NOT_NULL(strstr(output, "kgd=0B"));
    TEST_ASSERT_NOT_NULL(strstr(output, "not the part on the schematic"));
}

void test_psram_stays_quiet_about_identity_when_the_expected_part_is_fitted(void) {
    bsp_memory_report_t memory = healthy_memory();
    memory.psram_kgd = 0x5du;
    BSP_MemoryCheck_ExpectAndReturn(memory);

    TEST_ASSERT_NULL(strstr(run_step(STEP_PSRAM), "schematic"));
}

void test_psram_fails_when_the_pattern_is_lost(void) {
    bsp_memory_report_t memory = healthy_memory();
    memory.psram_ok = false;
    BSP_MemoryCheck_ExpectAndReturn(memory);

    const char *output = run_step(STEP_PSRAM);

    TEST_ASSERT_NOT_NULL(strstr(output, "FAIL"));
    TEST_ASSERT_NOT_NULL(strstr(output, "pattern LOST"));
}




/***** temperature, USB and watchdog *****/


void test_temperature_passes_inside_the_band_and_formats_a_negative_reading(void) {
    bsp_adc_temperature_t sample = {.raw = 800, .milli_celsius = 24500};
    BSP_AdcTemperature_ExpectAndReturn(sample);
    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_TEMPERATURE), "24.5C"));

    sample.milli_celsius = -5500;
    BSP_AdcTemperature_ExpectAndReturn(sample);
    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_TEMPERATURE), "-5.5C"));

    /* Truncation toward zero used to eat the sign here and print "0.5C" -- a
       wrong reading, on the one part of the scale where which side of freezing
       the board is on actually matters. */
    sample.milli_celsius = -500;
    BSP_AdcTemperature_ExpectAndReturn(sample);
    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_TEMPERATURE), "-0.5C"));
}

/* A reading pinned at a rail is the fault a band catches; the absolute figure is
   several degrees out on a good day and is not worth asserting on. */
void test_temperature_fails_outside_the_plausible_band(void) {
    bsp_adc_temperature_t cold = {.raw = 0, .milli_celsius = -60000};
    BSP_AdcTemperature_ExpectAndReturn(cold);
    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_TEMPERATURE), "FAIL"));

    bsp_adc_temperature_t hot = {.raw = 4095, .milli_celsius = 120000};
    BSP_AdcTemperature_ExpectAndReturn(hot);
    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_TEMPERATURE), "FAIL"));
}

/* Two samples twenty milliseconds apart: a single frame number proves nothing. */
void test_usb_passes_when_the_frame_counter_advances(void) {
    MOCK_BSP_UsbSetHealth(usb_health(100, true, false, 256));
    MOCK_BSP_TimeSetMs(1000);
    const application_activity_t *activity = application_ibit_single(STEP_USB);
    activity->start();
    MOCK_BSP_ConsoleReset();

    TEST_ASSERT_TRUE(activity->poll());
    TEST_ASSERT_TRUE(activity->poll());

    MOCK_BSP_TimeSetMs(1020);
    MOCK_BSP_UsbSetHealth(usb_health(120, true, false, 256));
    TEST_ASSERT_FALSE(activity->poll());

    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "sof advancing"));
    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "PASS"));
}

void test_usb_fails_when_the_frame_counter_is_frozen(void) {
    MOCK_BSP_UsbSetHealth(usb_health(100, true, false, 256));
    MOCK_BSP_TimeSetMs(1000);
    const application_activity_t *activity = application_ibit_single(STEP_USB);
    activity->start();
    MOCK_BSP_ConsoleReset();

    TEST_ASSERT_TRUE(activity->poll());
    MOCK_BSP_TimeSetMs(1020);
    TEST_ASSERT_FALSE(activity->poll());

    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "sof FROZEN"));
    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "FAIL"));
}

/* Each of the four conditions on its own, because a link can be unhealthy in
   exactly one way and an && that is only ever tested with everything wrong at
   once would never notice three of them. */
static void run_usb_step_with(bool connected, bool suspended, uint32_t write_available,
                              uint32_t second_frame) {
    MOCK_BSP_UsbSetHealth(usb_health(100, connected, suspended, write_available));
    MOCK_BSP_TimeSetMs(1000);
    const application_activity_t *activity = application_ibit_single(STEP_USB);
    activity->start();
    MOCK_BSP_ConsoleReset();

    TEST_ASSERT_TRUE(activity->poll());
    MOCK_BSP_TimeSetMs(1020);
    MOCK_BSP_UsbSetHealth(usb_health(second_frame, connected, suspended, write_available));
    TEST_ASSERT_FALSE(activity->poll());
}

void test_usb_fails_on_each_unhealthy_condition_in_isolation(void) {
    run_usb_step_with(false, false, 256, 120);
    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "FAIL"));

    run_usb_step_with(true, true, 256, 120);
    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "FAIL"));

    run_usb_step_with(true, false, 0, 120);
    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "FAIL"));
}

void test_watchdog_passes_after_a_clean_power_up(void) {
    application_diagnostics_boot_reason_ExpectAndReturn(BSP_BOOT_POWER_ON);

    const char *output = run_step(STEP_WATCHDOG);

    TEST_ASSERT_NOT_NULL(strstr(output, "PASS"));
    TEST_ASSERT_NOT_NULL(strstr(output, "last boot power-on"));
    TEST_ASSERT_NOT_NULL(strstr(output, "marker readback ok"));
}

/* The whole watchdog diagnosis rests on that scratch register holding a value
   across a reset, so a register that accepts a write and drops it has to be a
   failure and not a footnote. */
void test_watchdog_fails_when_the_marker_does_not_read_back(void) {
    application_diagnostics_boot_reason_ExpectAndReturn(BSP_BOOT_POWER_ON);
    MOCK_BSP_WatchdogSetMarkerReadbackFaulty(true);

    const char *output = run_step(STEP_WATCHDOG);

    TEST_ASSERT_NOT_NULL(strstr(output, "FAIL"));
    TEST_ASSERT_NOT_NULL(strstr(output, "marker readback BAD"));
}

/* The board is plainly running now, but something stopped feeding the loop and
   the retained marker is the only witness to where. */
void test_watchdog_fails_when_the_previous_boot_was_forced_by_the_watchdog(void) {
    application_diagnostics_boot_reason_ExpectAndReturn(BSP_BOOT_WATCHDOG);

    const char *output = run_step(STEP_WATCHDOG);

    TEST_ASSERT_NOT_NULL(strstr(output, "FAIL"));
    TEST_ASSERT_NOT_NULL(strstr(output, "last boot watchdog"));
}




/***** the FPGA and what sits behind it *****/


void test_fpga_configuration_passes_and_implies_the_oscillator(void) {
    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_FpgaStatusPin_ExpectAndReturn(true);

    const char *output = run_step(STEP_FPGA_CONFIGURATION);

    TEST_ASSERT_NOT_NULL(strstr(output, "PASS"));
    TEST_ASSERT_NOT_NULL(strstr(output, "cdone=1 id=B5"));
    TEST_ASSERT_NOT_NULL(strstr(output, "32MHz oscillator implied"));
}

void test_fpga_configuration_fails_on_a_wrong_design_id(void) {
    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(0x00u);
    BSP_FpgaStatusPin_ExpectAndReturn(false);

    TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_FPGA_CONFIGURATION), "FAIL"));
}

/* A walking pattern, because 0x00 and 0xFF are what a bus stuck low or high
   returns and either would pass a test that wrote them. */
void test_fpga_register_bus_round_trips_a_walking_pattern_and_restores_the_colour(void) {
    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_FpgaReadStatus_ExpectAndReturn(0x01u);
    BSP_LedGet_ExpectAndReturn(led_state(1, 2, 3, 4));
    BSP_LedSet_Expect(0x5au, 0xa5u, 0x3cu, 0xc3u);
    BSP_LedGet_ExpectAndReturn(led_state(0x5au, 0xa5u, 0x3cu, 0xc3u));
    BSP_LedSet_Expect(1, 2, 3, 4);

    const char *output = run_step(STEP_FPGA_REGISTERS);

    TEST_ASSERT_NOT_NULL(strstr(output, "PASS"));
    TEST_ASSERT_NOT_NULL(strstr(output, "wrote 5A,A5,3C,C3 read 5A,A5,3C,C3"));
}

/* One wrong byte at a time. A bus can corrupt a single register, and a test that
   only ever sees all four wrong together would pass three of those faults. */
void test_fpga_register_bus_fails_when_any_single_register_misreads(void) {
    const bsp_led_state_t corrupted[] = {
        led_state(0x00u, 0xa5u, 0x3cu, 0xc3u),
        led_state(0x5au, 0x00u, 0x3cu, 0xc3u),
        led_state(0x5au, 0xa5u, 0x00u, 0xc3u),
        led_state(0x5au, 0xa5u, 0x3cu, 0x00u),
    };

    for (uint32_t index = 0; index < 4u; ++index) {
        BSP_FpgaCdone_ExpectAndReturn(true);
        BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
        BSP_FpgaReadStatus_ExpectAndReturn(0x01u);
        BSP_LedGet_ExpectAndReturn(led_state(1, 2, 3, 4));
        BSP_LedSet_Expect(0x5au, 0xa5u, 0x3cu, 0xc3u);
        BSP_LedGet_ExpectAndReturn(corrupted[index]);
        BSP_LedSet_Expect(1, 2, 3, 4);

        TEST_ASSERT_NOT_NULL(strstr(run_step(STEP_FPGA_REGISTERS), "FAIL"));
    }
}

/* The skip decision is this run's evidence, not what bring-up recorded. An FPGA
   that answers CDONE but returns the wrong design ID is not one the LED and
   button tests can say anything about either. */
void test_a_wrong_design_id_also_stands_the_dependent_steps_down(void) {
    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(0x00u);

    const char *output = run_step(STEP_LED);

    TEST_ASSERT_NOT_NULL(strstr(output, "SKIP"));
    TEST_ASSERT_NOT_NULL(strstr(output, "FPGA not responding; this test sits behind it"));
}

/* Skipped rather than failed: the LED and the button are both behind the FPGA,
   so a verdict on either would describe the bus and not the part being named. */
void test_steps_behind_the_fpga_are_skipped_when_it_is_unreachable(void) {
    const uint32_t behind[] = {STEP_FPGA_REGISTERS, STEP_LED, STEP_BUTTON};

    for (uint32_t index = 0; index < 3u; ++index) {
        BSP_FpgaCdone_ExpectAndReturn(false);
        const char *output = run_step(behind[index]);
        TEST_ASSERT_NOT_NULL(strstr(output, "SKIP"));
        TEST_ASSERT_NOT_NULL(strstr(output, "FPGA not responding; this test sits behind it"));
    }
}

static void expect_led_phase(uint8_t red, uint8_t green, uint8_t blue) {
    BSP_LedSet_Expect(red, green, blue, 128);
    BSP_LedGet_ExpectAndReturn(led_state(red, green, blue, 128));
}

void test_led_drives_each_channel_in_turn_and_restores_the_previous_colour(void) {
    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_LedGet_ExpectAndReturn(led_state(9, 8, 7, 6));
    expect_led_phase(255, 0, 0);
    expect_led_phase(0, 255, 0);
    expect_led_phase(0, 0, 255);
    expect_led_phase(255, 255, 255);
    expect_led_phase(0, 0, 0);
    BSP_LedSet_Expect(9, 8, 7, 6);

    MOCK_BSP_TimeSetMs(1000);
    const application_activity_t *activity = application_ibit_single(STEP_LED);
    activity->start();
    MOCK_BSP_ConsoleReset();

    /* A pass with no time elapsed must not advance the phase, or the whole show
       would flash past in one iteration of the foreground loop. */
    TEST_ASSERT_TRUE(activity->poll());
    TEST_ASSERT_TRUE(activity->poll());
    for (uint32_t phase = 1; phase < 5u; ++phase) {
        MOCK_BSP_TimeSetMs(1000 + phase * 200u);
        TEST_ASSERT_TRUE(activity->poll());
    }
    MOCK_BSP_TimeSetMs(2000);
    TEST_ASSERT_FALSE(activity->poll());

    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "PASS"));
    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "previous colour restored"));
}

/* Each channel checked on its own, so a single dead colour is caught rather than
   only the case where all three fail together. */
void test_led_fails_and_restores_the_colour_when_any_channel_does_not_read_back(void) {
    const bsp_led_state_t corrupted[] = {
        led_state(0, 0, 0, 128),
        led_state(255, 99, 0, 128),
        led_state(255, 0, 99, 128),
    };

    for (uint32_t index = 0; index < 3u; ++index) {
        BSP_FpgaCdone_ExpectAndReturn(true);
        BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
        BSP_LedGet_ExpectAndReturn(led_state(9, 8, 7, 6));
        BSP_LedSet_Expect(255, 0, 0, 128);
        BSP_LedGet_ExpectAndReturn(corrupted[index]);
        BSP_LedSet_Expect(9, 8, 7, 6);

        const char *output = run_step(STEP_LED);

        TEST_ASSERT_NOT_NULL(strstr(output, "FAIL"));
        TEST_ASSERT_NOT_NULL(strstr(output, "readback mismatch at step 0"));
    }
}

/* The count is the FPGA's own debounced edge count, and it is what decides. */
void test_button_passes_when_the_count_moves_and_the_level_is_seen(void) {
    bsp_button_state_t before = {.level = 1, .count = 0};
    bsp_button_state_t pressed = {.level = 0, .count = 1};
    bsp_button_state_t holding = {.level = 0, .count = 0};

    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_ButtonClearCount_Expect();
    BSP_ButtonGetState_ExpectAndReturn(before);
    BSP_ButtonGetState_ExpectAndReturn(holding);
    BSP_ButtonGetState_ExpectAndReturn(pressed);

    MOCK_BSP_TimeSetMs(1000);
    const application_activity_t *activity = application_ibit_single(STEP_BUTTON);
    activity->start();
    MOCK_BSP_ConsoleReset();

    TEST_ASSERT_TRUE(activity->poll());
    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "press SW1 within 15s"));

    /* Held down across two samples, so the second one finds the level already
       known to have moved and does not need to look again. */
    TEST_ASSERT_TRUE(activity->poll());
    MOCK_BSP_TimeSetMs(3400);
    TEST_ASSERT_FALSE(activity->poll());

    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "PASS"));
    TEST_ASSERT_NOT_NULL(
        strstr(MOCK_BSP_ConsoleOutput(), "pressed after 2.4s, count 0 -> 1, level seen to move"));
}

/* The regression: a tap shorter than the 50 ms poll interval increments the
   FPGA's counter and is back at rest before the level is next read. That is a
   working button, and gating on the level reported it as a timeout. */
void test_button_passes_on_a_tap_too_brief_for_the_level_to_be_sampled(void) {
    bsp_button_state_t before = {.level = 1, .count = 0};
    bsp_button_state_t after = {.level = 1, .count = 1};

    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_ButtonClearCount_Expect();
    BSP_ButtonGetState_ExpectAndReturn(before);
    BSP_ButtonGetState_ExpectAndReturn(after);

    MOCK_BSP_TimeSetMs(1000);
    const application_activity_t *activity = application_ibit_single(STEP_BUTTON);
    activity->start();
    MOCK_BSP_ConsoleReset();

    TEST_ASSERT_TRUE(activity->poll());
    MOCK_BSP_TimeSetMs(1100);
    TEST_ASSERT_FALSE(activity->poll());

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL(strstr(output, "PASS"));
    TEST_ASSERT_NOT_NULL(strstr(output, "level never sampled moving"));
}

/* An unattended run has to finish, so silence is a timeout and not a failure. */
void test_button_times_out_without_failing_when_nobody_presses_it(void) {
    bsp_button_state_t idle = {.level = 1, .count = 4};

    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_ButtonClearCount_Expect();
    BSP_ButtonGetState_ExpectAndReturn(idle);
    BSP_ButtonGetState_ExpectAndReturn(idle);
    BSP_ButtonGetState_ExpectAndReturn(idle);

    MOCK_BSP_TimeSetMs(1000);
    const application_activity_t *activity = application_ibit_single(STEP_BUTTON);
    activity->start();
    MOCK_BSP_ConsoleReset();

    TEST_ASSERT_TRUE(activity->poll());
    TEST_ASSERT_TRUE(activity->poll());
    /* Between poll intervals there is nothing to ask the FPGA, which is what
       keeps a fifteen-second wait from being fifteen thousand bus transactions. */
    MOCK_BSP_TimeSetMs(1020);
    TEST_ASSERT_TRUE(activity->poll());
    MOCK_BSP_TimeSetMs(1000 + APPLICATION_IBIT_BUTTON_TIMEOUT_MS);
    TEST_ASSERT_FALSE(activity->poll());

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL(strstr(output, "TIMEOUT"));
    TEST_ASSERT_NULL(strstr(output, "FAIL"));
    TEST_ASSERT_NOT_NULL(strstr(output, "no press within 15s"));
}




/***** the sequence, the soak and the report *****/


/* Every step reachable without the FPGA, so the summary line and the tally are
   exercised across all five outcomes in one run. */
static void expect_sequence_without_the_fpga(void) {
    BSP_McuInfo_ExpectAndReturn(healthy_mcu());
    BSP_McuInfo_ExpectAndReturn(healthy_mcu());
    BSP_ClocksReport_ExpectAndReturn(healthy_clocks());
    BSP_McuInfo_ExpectAndReturn(healthy_mcu());
    BSP_McuInfo_ExpectAndReturn(healthy_mcu());
    BSP_MemoryCheck_ExpectAndReturn(healthy_memory());
    BSP_AdcTemperature_ExpectAndReturn(temperature_sample(800, 24500));
    BSP_FpgaCdone_ExpectAndReturn(false);
    BSP_FpgaPing_ExpectAndReturn(0x00u);
    BSP_FpgaStatusPin_ExpectAndReturn(false);
    BSP_FpgaCdone_ExpectAndReturn(false);
    BSP_FpgaCdone_ExpectAndReturn(false);
    BSP_FpgaCdone_ExpectAndReturn(false);
}

static void drive_to_completion(const application_activity_t *activity, uint32_t from_ms,
                                uint32_t to_ms) {
    uint32_t now_ms = from_ms;
    while (activity->poll()) {
        now_ms += 100u;
        MOCK_BSP_TimeSetMs(now_ms > to_ms ? to_ms : now_ms);
    }
}

/* A board with a dead FPGA and a frozen start-of-frame counter: two real faults,
   three tests that cannot mean anything without the FPGA, and one measurement
   with no pass criterion. The point is that all four land in different columns
   rather than collapsing into one number. */
void test_the_sequence_runs_every_step_and_summarises_the_tally(void) {
    application_diagnostics_boot_reason_IgnoreAndReturn(BSP_BOOT_POWER_ON);
    MOCK_BSP_UsbSetHealth(usb_health(100, true, false, 256));
    expect_sequence_without_the_fpga();

    MOCK_BSP_TimeSetMs(1000);
    const application_activity_t *activity = application_ibit_sequence();
    activity->start();
    drive_to_completion(activity, 1000, 60000);

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL(strstr(output, "Initiated built-in test"));
    TEST_ASSERT_NOT_NULL(strstr(output, "[ 1/14] Chip identity"));
    TEST_ASSERT_NOT_NULL(strstr(output, "[14/14] Button SW1"));
    TEST_ASSERT_NOT_NULL(
        strstr(output, "IBIT: 8 PASS  2 FAIL  0 TIMEOUT  3 SKIP  1 INFO"));
}

/* The soak never finishes on its own; it is stopped by the abort path, which is
   also what has to put the LED back. */
void test_the_soak_tallies_across_iterations_and_starts_the_next_run(void) {
    application_diagnostics_boot_reason_IgnoreAndReturn(BSP_BOOT_POWER_ON);
    MOCK_BSP_UsbSetHealth(usb_health(100, true, false, 256));
    expect_sequence_without_the_fpga();

    MOCK_BSP_TimeSetMs(1000);
    const application_activity_t *activity = application_ibit_soak();
    activity->start();

    uint32_t now_ms = 1000;
    while (strstr(MOCK_BSP_ConsoleOutput(), "soak: 1 run(s)") == NULL) {
        TEST_ASSERT_TRUE(activity->poll());
        now_ms += 100u;
        MOCK_BSP_TimeSetMs(now_ms > 60000u ? 60000u : now_ms);
    }

    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "soak: 1 run(s), 1 with a failure, 0 with a timeout"));
    activity->stop();
}

void test_aborting_after_the_led_step_puts_the_previous_colour_back(void) {
    BSP_FpgaCdone_ExpectAndReturn(true);
    BSP_FpgaPing_ExpectAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_LedGet_ExpectAndReturn(led_state(9, 8, 7, 6));
    expect_led_phase(255, 0, 0);

    MOCK_BSP_TimeSetMs(1000);
    const application_activity_t *activity = application_ibit_single(STEP_LED);
    activity->start();
    TEST_ASSERT_TRUE(activity->poll());

    BSP_LedSet_Expect(9, 8, 7, 6);
    activity->stop();

    /* A second stop must not write again; there is nothing left saved. */
    activity->stop();
}

/* Answers in the order one healthy sequence asks: the register-bus step reads
   the current colour and then its own pattern back, and the LED step reads the
   colour to restore followed by each of the five it drives. */
static bsp_led_state_t led_get_callback(int num_calls) {
    switch (num_calls) {
    case 0:
        return led_state(9, 8, 7, 6);
    case 1:
        return led_state(0x5au, 0xa5u, 0x3cu, 0xc3u);
    case 2:
        return led_state(9, 8, 7, 6);
    case 3:
        return led_state(255, 0, 0, 128);
    case 4:
        return led_state(0, 255, 0, 128);
    case 5:
        return led_state(0, 0, 255, 128);
    case 6:
        return led_state(255, 255, 255, 128);
    default:
        return led_state(0, 0, 0, 128);
    }
}

static bsp_button_state_t button_callback(int num_calls) {
    bsp_button_state_t before = {.level = 1, .count = 4};
    bsp_button_state_t pressed = {.level = 0, .count = 5};
    return num_calls == 0 ? before : pressed;
}

static void ignore_a_healthy_board(void) {
    BSP_McuInfo_IgnoreAndReturn(healthy_mcu());
    BSP_ClocksReport_IgnoreAndReturn(healthy_clocks());
    BSP_MemoryCheck_IgnoreAndReturn(healthy_memory());
    BSP_AdcTemperature_IgnoreAndReturn(temperature_sample(800, 24500));
    BSP_FpgaCdone_IgnoreAndReturn(true);
    BSP_FpgaPing_IgnoreAndReturn(BSP_FPGA_DESIGN_ID);
    BSP_FpgaStatusPin_IgnoreAndReturn(true);
    BSP_FpgaReadStatus_IgnoreAndReturn(0x01u);
    BSP_LedSet_Ignore();
    BSP_LedGet_StubWithCallback(led_get_callback);
    BSP_ButtonClearCount_Ignore();
    BSP_ButtonGetState_StubWithCallback(button_callback);
    application_diagnostics_boot_reason_IgnoreAndReturn(BSP_BOOT_POWER_ON);
}

/* The soak counts iterations that had a failure, not iterations, so a clean run
   has to leave the failure tally alone. */
void test_a_clean_soak_iteration_does_not_count_as_a_failure(void) {
    ignore_a_healthy_board();

    uint32_t frame = 100;
    MOCK_BSP_UsbSetHealth(usb_health(frame, true, false, 256));
    MOCK_BSP_TimeSetMs(1000);
    const application_activity_t *activity = application_ibit_soak();
    activity->start();

    uint32_t now_ms = 1000;
    while (strstr(MOCK_BSP_ConsoleOutput(), "soak: 1 run(s)") == NULL) {
        TEST_ASSERT_TRUE(activity->poll());
        now_ms += 100u;
        MOCK_BSP_TimeSetMs(now_ms);
        MOCK_BSP_UsbSetHealth(usb_health(++frame, true, false, 256));
    }

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL(strstr(output, "IBIT: 13 PASS  0 FAIL  0 TIMEOUT  0 SKIP  1 INFO"));
    TEST_ASSERT_NOT_NULL(strstr(output, "soak: 1 run(s), 0 with a failure, 0 with a timeout"));
    activity->stop();
}

static bsp_button_state_t button_never_pressed_callback(int num_calls) {
    (void)num_calls;
    bsp_button_state_t idle = {.level = 1, .count = 4};
    return idle;
}

/* The case a real burn-in is: nobody is at the bench, so the button times out
   every iteration. That must not read as a failing run, or the one number a soak
   exists to produce equals the run count forever and says nothing. */
void test_an_unattended_soak_iteration_counts_a_timeout_and_not_a_failure(void) {
    ignore_a_healthy_board();
    BSP_ButtonGetState_StubWithCallback(button_never_pressed_callback);

    uint32_t frame = 100;
    MOCK_BSP_UsbSetHealth(usb_health(frame, true, false, 256));
    MOCK_BSP_TimeSetMs(1000);
    const application_activity_t *activity = application_ibit_soak();
    activity->start();

    uint32_t now_ms = 1000;
    while (strstr(MOCK_BSP_ConsoleOutput(), "soak: 1 run(s)") == NULL) {
        TEST_ASSERT_TRUE(activity->poll());
        now_ms += 100u;
        MOCK_BSP_TimeSetMs(now_ms);
        MOCK_BSP_UsbSetHealth(usb_health(++frame, true, false, 256));
    }

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL(strstr(output, "12 PASS  0 FAIL  1 TIMEOUT"));
    TEST_ASSERT_NOT_NULL(strstr(output, "soak: 1 run(s), 0 with a failure, 1 with a timeout"));
    activity->stop();
}

void test_step_names_are_published_for_the_menu(void) {
    TEST_ASSERT_EQUAL_UINT32(14, application_ibit_step_count());
    TEST_ASSERT_EQUAL_STRING("Chip identity", application_ibit_step_name(STEP_CHIP_IDENTITY));
    TEST_ASSERT_EQUAL_STRING("Button SW1", application_ibit_step_name(STEP_BUTTON));
}

void test_the_board_report_states_the_facts_without_judging_them(void) {
    BSP_McuInfo_ExpectAndReturn(healthy_mcu());
    BSP_ClocksReport_ExpectAndReturn(healthy_clocks());
    BSP_AdcTemperature_ExpectAndReturn(temperature_sample(800, 24500));

    application_ibit_print_board_report();

    const char *output = MOCK_BSP_ConsoleOutput();
    TEST_ASSERT_NOT_NULL(strstr(output, "manufacturer=493 part=0004"));
    TEST_ASSERT_NOT_NULL(strstr(output, "board    E66038B7135F212C"));
    TEST_ASSERT_NOT_NULL(strstr(output, "package  id=11223344 device=01020304AABBCCDD valid=1"));
    TEST_ASSERT_NOT_NULL(strstr(output, "otp_cs0=0x9"));
    TEST_ASSERT_NOT_NULL(strstr(output, "measured sys=150000000"));
    TEST_ASSERT_NOT_NULL(strstr(output, "24500 milli-degrees C"));
    TEST_ASSERT_NULL(strstr(output, "PASS"));
}

void test_the_board_report_names_a_risc_v_image(void) {
    bsp_mcu_info_t info = healthy_mcu();
    info.architecture = BSP_MCU_ARCHITECTURE_RISCV;
    BSP_McuInfo_ExpectAndReturn(info);
    BSP_ClocksReport_ExpectAndReturn(healthy_clocks());
    BSP_AdcTemperature_ExpectAndReturn(temperature_sample(800, 24500));

    application_ibit_print_board_report();

    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "RISC-V"));
}
