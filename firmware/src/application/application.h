#ifndef FORGIX_APPLICATION_H
#define FORGIX_APPLICATION_H

#include "bsp.h"

void application_init(const bsp_init_result_t *bsp_result);
void application_process_command(char *command);
void application_run(void);

#endif
