#include "application.h"
#include "bsp.h"

int main( void )
{
    bsp_init_result_t bsp_result = BSP_Init();
    application_init( &bsp_result );
    application_run();
}
