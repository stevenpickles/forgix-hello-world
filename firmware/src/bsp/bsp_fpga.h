#ifndef FORGIX_BSP_FPGA_H
#define FORGIX_BSP_FPGA_H

#include <stdbool.h>
#include <stdint.h>

enum { BSP_FPGA_DESIGN_ID = 0xb5 };

typedef struct {
    bool configured;
    uint8_t design_id;
    bool ready;
    bool cdone;
    bool status_pin;
} bsp_fpga_init_result_t;

bsp_fpga_init_result_t bsp_fpga_init(void);
bool bsp_fpga_is_ready(void);

/* Configuration-done pin. Low at runtime means the FPGA lost its configuration,
   which the diagnostics layer treats as a recoverable hardware fault. */
bool bsp_fpga_cdone(void);

/* Reloads the embedded bitstream and revalidates the design ID. Returns true
   when the FPGA is responding again. */
bool bsp_fpga_reconfigure(void);

uint8_t bsp_fpga_ping(void);
uint8_t bsp_fpga_read_status(void);
bool bsp_fpga_status_pin(void);
void bsp_fpga_reset(void);

uint8_t bsp_fpga_read_register(uint8_t address);
void bsp_fpga_write_register(uint8_t address, uint8_t value);

#endif
