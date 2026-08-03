#include "application_ui.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "application_console.h"
#include "application_diagnostics.h"
#include "bsp.h"

typedef enum {
    UI_MODE_BANNER,
    UI_MODE_MENU,
    UI_MODE_SHELL,
} ui_mode_t;

typedef struct {
    ui_mode_t mode;
    uint32_t current_time_ms;
    uint32_t next_banner_ms;
    uint32_t banner_count;
    uint32_t started_ms;
} ui_state_t;

typedef struct {
    char key;
    const char *label;
    const char *detail;
    void (*action)(void);
} menu_entry_t;

static ui_state_t ui;

static void action_shell(void);
static void action_reboot(void);
static void action_bootsel(void);
static void action_redraw(void);

/* One table drives both the rendering and the dispatch, so a key can never be
   offered without doing something or do something without being offered. */
static const menu_entry_t MENU[] = {
    {'c', "Command shell", "the forgix> prompt; `menu` returns here", action_shell},
    {'r', "Reboot", "restart the board and reconfigure the FPGA", action_reboot},
    {'b', "Reboot to BOOTSEL", "hand the board to the USB loader for reflashing", action_bootsel},
    {'?', "Redraw this menu", "", action_redraw},
};

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

/* Every console write reaches the untimed Pico SDK stdio flush loop, so the
   marker is set immediately before the call, exactly as the shell does. */
static void mark_write(void) {
    BSP_WatchdogMarkerSet(APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE);
}

static uint32_t uptime_seconds(void) {
    return (ui.current_time_ms - ui.started_ms) / 1000u;
}

static void print_banner(void) {
    mark_write();
    BSP_ConsolePrintf("hello world - %lu - press any key\n",
                       (unsigned long)ui.banner_count);
}

static void print_menu(void) {
    mark_write();
    BSP_ConsolePrintf("\n=== Forgix menu ===   up %lus   FPGA %s\n\n",
                       (unsigned long)uptime_seconds(),
                       BSP_FpgaIsReady() ? "ready" : "UNAVAILABLE");
    for (size_t index = 0; index < sizeof MENU / sizeof MENU[0]; ++index) {
        mark_write();
        BSP_ConsolePrintf("  %c  %-24s %s\n", MENU[index].key, MENU[index].label,
                           MENU[index].detail);
    }
    mark_write();
    BSP_ConsolePrintf("\nselect> ");
}

static void enter_menu(void) {
    ui.mode = UI_MODE_MENU;
    print_menu();
}

static void action_redraw(void) {
    enter_menu();
}

static void action_shell(void) {
    ui.mode = UI_MODE_SHELL;
    application_console_start();
}

static void action_reboot(void) {
    mark_write();
    BSP_ConsolePrintf("rebooting\n");
    BSP_McuReboot();
}

static void action_bootsel(void) {
    mark_write();
    BSP_ConsolePrintf("entering BOOTSEL; the serial port will disappear\n");
    BSP_McuRebootToBootsel();
}

/* An unrecognized key redraws rather than complaining. The menu is the only
   thing on screen that says which keys exist, so showing it again is both the
   error message and the fix. */
static void select_entry(int16_t character) {
    for (size_t index = 0; index < sizeof MENU / sizeof MENU[0]; ++index) {
        if (MENU[index].key == (char)character) {
            MENU[index].action();
            return;
        }
    }
    enter_menu();
}

void application_ui_start(void) {
    ui = (ui_state_t){.mode = UI_MODE_BANNER};
    ui.current_time_ms = BSP_TimeNowMs();
    ui.started_ms = ui.current_time_ms;
    ui.next_banner_ms = ui.current_time_ms;
}

void application_ui_enter_menu(void) {
    enter_menu();
}

void application_ui_poll(void) {
    BSP_WatchdogMarkerSet(APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_READ);
    int16_t character = BSP_ConsoleGetCharTimeoutUs(1000);
    ui.current_time_ms = BSP_TimeNowMs();

    if (ui.mode == UI_MODE_SHELL) {
        if (character != BSP_CONSOLE_TIMEOUT) {
            application_console_feed(character);
        } else {
            application_console_idle();
        }
        return;
    }

    if (character != BSP_CONSOLE_TIMEOUT) {
        if (ui.mode == UI_MODE_BANNER) {
            /* The key that ends the banner is consumed by ending it. Treating it
               as a selection as well would fire whichever item the user happened
               to hit while reaching for any key at all. */
            enter_menu();
        } else {
            select_entry(character);
        }
        return;
    }

    if (ui.mode != UI_MODE_BANNER ||
            !deadline_reached(ui.current_time_ms, ui.next_banner_ms)) {
        return;
    }

    ui.next_banner_ms = ui.current_time_ms + APPLICATION_UI_BANNER_PERIOD_MS;
    ++ui.banner_count;

    /* The count advances whether or not anyone is listening, so it reads as
       uptime rather than as a byte count. Only the writing is gated on DTR:
       pushing into a port no host has opened is the one unbounded trip through
       the untimed stdio flush loop this firmware can inflict on itself. */
    if (BSP_UsbConnected()) {
        print_banner();
    }
}
