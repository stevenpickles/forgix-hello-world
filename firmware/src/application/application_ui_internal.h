#ifndef FORGIX_APPLICATION_UI_INTERNAL_H
#define FORGIX_APPLICATION_UI_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include <stdint.h>

#include "application_ui.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


/* The seam between application_ui.c, which owns the mode machine, and
   application_ui_menu.c, which owns what the menu looks like and what its keys
   do. Both halves read and write one singleton rather than passing it around:
   the menu draws the mode it is about to enter, so a second copy would let the
   two files disagree about which mode is current. Nothing outside those two
   files may include this header -- application_ui.h is the UI's public face. */

typedef enum
{
    APPLICATION_UI_MODE_BANNER,
    APPLICATION_UI_MODE_MENU,
    APPLICATION_UI_MODE_STEPS,
    APPLICATION_UI_MODE_ACTIVITY,
    APPLICATION_UI_MODE_SHELL,
} ui_mode_t;


typedef struct
{
    ui_mode_t mode;
    uint32_t current_time_ms;
    uint32_t next_banner_ms;
    uint32_t banner_count;
    uint32_t started_ms;
    const application_activity_t *activity;
} ui_state_t;


extern ui_state_t ui;




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


void application_ui_mark_write( void );

uint32_t application_ui_uptime_seconds( void );

void application_ui_start_activity( const application_activity_t *activity );

#ifdef __cplusplus
}
#endif

#endif
