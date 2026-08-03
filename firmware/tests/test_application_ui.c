#include "unity.h"

#include <stdint.h>
#include <string.h>

#include "application_diagnostics.h"
#include "application_ui.h"
#include "mock_bsp_console.h"
#include "mock_bsp_time.h"
#include "mock_bsp_usb.h"
#include "mock_bsp_watchdog.h"
#include "mock_auto_application_console.h"
#include "mock_auto_bsp_button.h"
#include "mock_auto_bsp_fpga.h"
#include "mock_auto_bsp_led.h"
#include "mock_auto_bsp_mcu.h"
#include "mock_auto_bsp_memory.h"

void setUp(void) {
    MOCK_BSP_ConsoleReset();
    MOCK_BSP_TimeReset();
    MOCK_BSP_UsbReset();
    MOCK_BSP_WatchdogReset();
}

void tearDown(void) {
}

static void start_at(uint32_t now_ms) {
    MOCK_BSP_TimeSetMs(now_ms);
    application_ui_start();
}

static void poll_at(uint32_t now_ms) {
    MOCK_BSP_TimeSetMs(now_ms);
    application_ui_poll();
}

static void key_at(char key, uint32_t now_ms) {
    MOCK_BSP_ConsoleQueueCharacter((uint8_t)key);
    poll_at(now_ms);
}

/* Drives a fresh boot and the banner-dismissing keypress, so the tests that care
   about menu behavior neither repeat the way in nor inherit the mode a previous
   test left in the module's static state. */
static void open_menu_at(uint32_t now_ms) {
    start_at(now_ms);
    BSP_FpgaIsReady_ExpectAndReturn(true);
    key_at(' ', now_ms);
    MOCK_BSP_ConsoleReset();
}

void test_banner_repeats_once_a_second_until_a_key_arrives(void) {
    start_at(0);

    poll_at(0);
    poll_at(999);
    poll_at(1000);

    TEST_ASSERT_EQUAL_STRING(
        "hello world - 1 - press any key\n"
        "hello world - 2 - press any key\n",
        MOCK_BSP_ConsoleOutput());
}

/* The count is what tells a user how long the board has been up by the time they
   found the port, so it must advance while nobody is listening. */
void test_banner_counts_while_the_host_is_absent_so_it_reads_as_uptime(void) {
    MOCK_BSP_UsbSetConnected(false);
    start_at(0);

    poll_at(0);
    poll_at(1000);
    TEST_ASSERT_EQUAL_STRING("", MOCK_BSP_ConsoleOutput());

    MOCK_BSP_UsbSetConnected(true);
    poll_at(2000);
    TEST_ASSERT_EQUAL_STRING("hello world - 3 - press any key\n", MOCK_BSP_ConsoleOutput());
}

/* 'r' is the reboot key. Reaching for "any key" must not be able to fire it. */
void test_the_key_that_ends_the_banner_does_not_also_select_from_the_menu(void) {
    start_at(0);

    BSP_FpgaIsReady_ExpectAndReturn(true);
    key_at('r', 0);

    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "=== Forgix menu ==="));
    TEST_ASSERT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "rebooting"));
}

void test_menu_reports_uptime_and_a_healthy_fpga(void) {
    start_at(1000);

    BSP_FpgaIsReady_ExpectAndReturn(true);
    key_at(' ', 8000);

    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "up 7s"));
    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "FPGA ready"));
    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "select> "));
}

/* A dead FPGA must be visible on the menu itself. The tests that diagnose it are
   reached from here, so the user has to know before they choose. */
void test_menu_names_an_unavailable_fpga(void) {
    start_at(0);

    BSP_FpgaIsReady_ExpectAndReturn(false);
    key_at(' ', 0);

    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "FPGA UNAVAILABLE"));
}

void test_unknown_menu_key_redraws_rather_than_complaining(void) {
    open_menu_at(0);

    BSP_FpgaIsReady_ExpectAndReturn(true);
    key_at('z', 100);

    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "=== Forgix menu ==="));
}

void test_redraw_key_reprints_the_menu(void) {
    open_menu_at(0);

    BSP_FpgaIsReady_ExpectAndReturn(true);
    key_at('?', 100);

    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "Redraw this menu"));
}

void test_shell_key_hands_the_terminal_to_the_console(void) {
    open_menu_at(0);

    application_console_start_Expect();
    key_at('c', 100);

    application_console_feed_Expect('x');
    key_at('x', 200);

    application_console_idle_Expect();
    poll_at(300);
}

void test_menu_command_takes_the_terminal_back_from_the_shell(void) {
    open_menu_at(0);
    application_console_start_Expect();
    key_at('c', 100);
    MOCK_BSP_ConsoleReset();

    BSP_FpgaIsReady_ExpectAndReturn(true);
    application_ui_enter_menu();
    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "=== Forgix menu ==="));

    /* Back under the menu, a key selects again rather than reaching the shell. */
    BSP_FpgaIsReady_ExpectAndReturn(true);
    key_at('?', 200);
}

void test_reboot_key_warns_before_restarting_the_board(void) {
    open_menu_at(0);

    BSP_McuReboot_Expect();
    key_at('r', 100);

    TEST_ASSERT_EQUAL_STRING("rebooting\n", MOCK_BSP_ConsoleOutput());
}

void test_bootsel_key_warns_that_the_port_is_about_to_vanish(void) {
    open_menu_at(0);

    BSP_McuRebootToBootsel_Expect();
    key_at('b', 100);

    TEST_ASSERT_NOT_NULL(strstr(MOCK_BSP_ConsoleOutput(), "serial port will disappear"));
}

/* Nothing is due in the menu, so an empty poll must stay silent -- otherwise the
   menu would scroll itself off the screen while the user is reading it. */
void test_menu_stays_silent_while_it_waits(void) {
    open_menu_at(0);

    poll_at(60000);

    TEST_ASSERT_EQUAL_STRING("", MOCK_BSP_ConsoleOutput());
}

void test_ui_marks_the_read_path_for_the_watchdog(void) {
    start_at(0);

    poll_at(0);

    TEST_ASSERT_TRUE(
        MOCK_BSP_WatchdogMarkerWasWritten(APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_READ));
    TEST_ASSERT_TRUE(
        MOCK_BSP_WatchdogMarkerWasWritten(APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE));
}
