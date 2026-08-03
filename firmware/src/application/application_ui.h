#ifndef FORGIX_APPLICATION_UI_H
#define FORGIX_APPLICATION_UI_H

#include <stdbool.h>
#include <stdint.h>

enum {
    /* The banner repeats at this rate for as long as nobody has pressed a key.
       One second is fast enough that a user who has just found the port sees
       movement immediately, and slow enough that the count doubles as a
       readable uptime. */
    APPLICATION_UI_BANNER_PERIOD_MS = 1000,
};

void application_ui_start(void);
void application_ui_poll(void);

/* Leaves the shell and redraws the menu; reached by the shell's `menu` command.
   Declared here rather than in application.h because the shell is a guest of the
   UI layer, not the other way round. */
void application_ui_enter_menu(void);

#endif
