#ifndef FORGIX_APPLICATION_CONSOLE_INTERNAL_H
#define FORGIX_APPLICATION_CONSOLE_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


/* The seam between application_console.c, which owns the line editor, and
   application_console_status.c, which owns the unsolicited-status scheduler.
   The scheduler decides whether to print from the same state the editor writes
   -- a half-typed line and the quiet flag both suppress status -- so the two
   files share one singleton. Nothing outside that pair may include this header;
   application_console.h is the shell's public face. */

enum
{
    APPLICATION_CONSOLE_COMMAND_CAPACITY = 128
};

/* There is no boot mode here any more. Reporting status once a second until a
   key arrived used to be how the board proved it was alive; the banner the UI
   layer prints before the shell is ever entered does that job now, and does it
   in words a user who has just plugged the board in can act on. */
typedef enum
{
    APPLICATION_CONSOLE_STATUS_DISABLED,
    APPLICATION_CONSOLE_STATUS_IDLE,
    APPLICATION_CONSOLE_STATUS_WATCH,
} status_mode_t;

typedef struct
{
    char line[ APPLICATION_CONSOLE_COMMAND_CAPACITY ];
    size_t used;
    bool echo_enabled;
    bool quiet;
    bool released;
    bool auto_status_enabled;
    bool swallow_lf;
    status_mode_t status_mode;
    /* The mode a keystroke paused, so the completed line can put it back. Only
       the stop paths clear it: quiet, release and `watch off` end a watch,
       while a keystroke merely holds it for the length of a line. */
    status_mode_t paused_status_mode;
    uint32_t current_time_ms;
    uint32_t next_status_ms;
    uint32_t status_period_ms;
} console_state_t;


extern console_state_t application_console_state;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


void application_console_mark_write( void );

void application_console_print_prompt( void );

#ifdef __cplusplus
}
#endif

#endif
