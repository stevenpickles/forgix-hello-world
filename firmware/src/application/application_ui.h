#ifndef FORGIX_APPLICATION_UI_H
#define FORGIX_APPLICATION_UI_H

#include <stdbool.h>
#include <stdint.h>

enum
{
    /* The banner repeats at this rate for as long as nobody has pressed a key.
       One second is fast enough that a user who has just found the port sees
       movement immediately, and slow enough that the count doubles as a
       readable uptime. */
    APPLICATION_UI_BANNER_PERIOD_MS = 1000,
};

/* Anything the menu can start that does not finish inside one pass of the
   foreground loop. poll() is called once per pass and returns false when the
   activity is done; stop() runs when a keypress aborts it, and exists so an
   activity that left the LED or the FPGA registers somewhere unusual can put
   them back. Both are mandatory -- an activity with nothing to undo supplies an
   empty stop() rather than a null pointer, so there is no path where cleanup is
   silently skipped.

   The contract that matters: poll() must return promptly. The watchdog is fed
   once per foreground pass, so an activity that blocks is indistinguishable from
   the hang the watchdog exists to catch. */
typedef struct
{
    const char *name;
    void ( *start )( void );
    bool ( *poll )( void );
    void ( *stop )( void );
} application_activity_t;

void application_ui_start( void );
void application_ui_poll( void );

/* Leaves the shell and redraws the menu; reached by the shell's `menu` command.
   Declared here rather than in application.h because the shell is a guest of the
   UI layer, not the other way round. */
void application_ui_enter_menu( void );

#endif
