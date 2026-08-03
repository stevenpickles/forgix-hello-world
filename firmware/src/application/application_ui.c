#include "application_ui.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "application_console.h"
#include "application_diagnostics.h"
#include "application_ibit.h"
#include "bsp.h"

typedef enum {
    UI_MODE_BANNER,
    UI_MODE_MENU,
    UI_MODE_STEPS,
    UI_MODE_ACTIVITY,
    UI_MODE_SHELL,
} ui_mode_t;

typedef struct {
    ui_mode_t mode;
    uint32_t current_time_ms;
    uint32_t next_banner_ms;
    uint32_t banner_count;
    uint32_t started_ms;
    const application_activity_t *activity;
} ui_state_t;

typedef struct {
    char key;
    const char *label;
    const char *detail;
    void (*action)(void);
} menu_entry_t;

static ui_state_t ui;

static void action_ibit(void);
static void action_soak(void);
static void action_steps(void);
static void action_report(void);
static void action_shell(void);
static void action_reboot(void);
static void action_bootsel(void);
static void action_redraw(void);

/* One table drives both the rendering and the dispatch, so a key can never be
   offered without doing something or do something without being offered. */
static const menu_entry_t MENU[] = {
    {'1', "Built-in test", "the whole sequence, once", action_ibit},
    {'2', "Built-in test soak", "repeat with a tally until a key is pressed", action_soak},
    {'3', "One test at a time", "re-run a single step without the other thirteen", action_steps},
    {'4', "Board report", "what this board is, without judging it", action_report},
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
        BSP_ConsolePrintf("  %c  %-22s %s\n", MENU[index].key, MENU[index].label,
                           MENU[index].detail);
    }
    mark_write();
    BSP_ConsolePrintf("\nselect> ");
}

/* Steps are offered as 1..9 then a..e, because a single keypress is the whole
   input method and fourteen of them will not fit in the digits. */
static char step_key(uint32_t index) {
    return index < 9u ? (char)('1' + index) : (char)('a' + (index - 9u));
}

static void print_steps(void) {
    mark_write();
    BSP_ConsolePrintf("\n=== One test at a time ===\n\n");
    for (uint32_t index = 0; index < application_ibit_step_count(); ++index) {
        mark_write();
        BSP_ConsolePrintf("  %c  %s\n", step_key(index), application_ibit_step_name(index));
    }
    mark_write();
    BSP_ConsolePrintf("  x  back to the menu\n\nselect> ");
}

static void enter_menu(void) {
    ui.mode = UI_MODE_MENU;
    print_menu();
}

static void start_activity(const application_activity_t *activity) {
    ui.mode = UI_MODE_ACTIVITY;
    ui.activity = activity;
    activity->start();
}

static void finish_activity(void) {
    ui.activity = NULL;
    enter_menu();
}

static void action_redraw(void) {
    enter_menu();
}

static void action_ibit(void) {
    start_activity(application_ibit_sequence());
}

static void action_soak(void) {
    start_activity(application_ibit_soak());
}

static void action_steps(void) {
    ui.mode = UI_MODE_STEPS;
    print_steps();
}

static void action_report(void) {
    application_ibit_print_board_report();
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

static void select_step(int16_t character) {
    if ((char)character == 'x') {
        enter_menu();
        return;
    }
    for (uint32_t index = 0; index < application_ibit_step_count(); ++index) {
        if (step_key(index) == (char)character) {
            start_activity(application_ibit_single(index));
            return;
        }
    }
    print_steps();
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

    if (ui.mode == UI_MODE_ACTIVITY) {
        /* Any key aborts. A user watching a test they no longer want should not
           have to remember which key means stop. */
        if (character != BSP_CONSOLE_TIMEOUT) {
            ui.activity->stop();
            mark_write();
            BSP_ConsolePrintf("\naborted\n");
            finish_activity();
        } else if (!ui.activity->poll()) {
            finish_activity();
        }
        return;
    }

    BSP_WatchdogMarkerSet(APPLICATION_DIAGNOSTICS_MARKER_MENU);

    if (character != BSP_CONSOLE_TIMEOUT) {
        if (ui.mode == UI_MODE_BANNER) {
            /* The key that ends the banner is consumed by ending it. Treating it
               as a selection as well would fire whichever item the user happened
               to hit while reaching for any key at all. */
            enter_menu();
        } else if (ui.mode == UI_MODE_STEPS) {
            select_step(character);
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
