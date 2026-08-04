#ifndef FORGIX_APPLICATION_H
#define FORGIX_APPLICATION_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp.h"




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


void application_init( const bsp_init_result_t *bsp_result );
void application_print_status( void );
void application_process_command( char *command );
void application_run( void );

#ifdef __cplusplus
}
#endif

#endif
