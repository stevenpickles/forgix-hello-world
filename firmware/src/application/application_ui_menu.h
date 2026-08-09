#ifndef FORGIX_APPLICATION_UI_MENU_H
#define FORGIX_APPLICATION_UI_MENU_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include <stdint.h>




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


/* What the menu is, as opposed to when it is on screen: one table drives both
   the drawing and the dispatch, so a key can never be offered without doing
   something or do something without being offered. application_ui.c decides
   which of these to call from the mode it is in, and never renders anything
   itself. */

void application_ui_menu_print( void );

void application_ui_menu_select_entry( int16_t character );

void application_ui_menu_select_step( int16_t character );

#ifdef __cplusplus
}
#endif

#endif
