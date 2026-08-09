#ifndef FORGIX_APPLICATION_DIAGNOSTICS_INTERNAL_H
#define FORGIX_APPLICATION_DIAGNOSTICS_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include <stdbool.h>
#include <stdint.h>

#include "bsp.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


/* The seam between the three files that make up the diagnostics module:
   application_diagnostics.c samples health, application_diagnostics_led.c turns
   that health into a colour, and application_diagnostics_report.c turns it into
   words and blinks. All three read one record of what the board has been doing
   since boot, and none of them owns enough of it to hand the others a copy.
   Nothing outside those three files may include this header. */

enum
{
    APPLICATION_DIAGNOSTICS_HEARTBEAT_BRIGHTNESS = 64
};

typedef struct
{
    bool usb_present;
    bsp_boot_reason boot_reason;
    uint32_t boot_marker;
    uint32_t boot_snapshot[ BSP_WATCHDOG_SNAPSHOT_SLOTS ];

    bool led_on;
    /* True while something else owns the LED. The phase keeps advancing
       underneath, so the heartbeat picks up where it would have been rather than
       restarting whenever an activity ends. */
    bool led_released;
    uint32_t next_led_ms;
    uint32_t next_sample_ms;
    uint32_t uptime_seconds;
    uint32_t recovery_toggles;
    bsp_led_state_t commanded;

    bsp_usb_health_t health;
    uint32_t last_tx_count;
    /* True while an unbroken run of samples has seen the transmit FIFO full
       with the TX completion counter unmoved. Cleared by observed room or by
       TX progress; inbound traffic does not touch it. */
    bool fifo_stalled;
    /* The timestamp of the first sample of that run -- the stall is measured
       from the moment fullness was first observed, not from the last sample
       that had room. Meaningful only while fifo_stalled is set. */
    uint32_t fifo_stall_epoch_ms;
    uint32_t last_frame_number;
    uint32_t last_frame_ms;

    uint32_t fpga_failures;
    /* Exactly one of the three below moves with every fpga_failures increment,
       naming the first term that failed: the configuration pin, the design-ID
       ping, or the LED register readback. The split is what turns "the check
       failed once a boot" from a mystery into a measurement. */
    uint32_t fpga_cdone_failures;
    uint32_t fpga_ping_failures;
    uint32_t fpga_readback_failures;
    /* Failing samples since the last passing one -- the debounce that keeps a
       single bus misread from revoking readiness for the rest of the boot. */
    uint32_t fpga_consecutive_failures;
    uint32_t fpga_reconfigures;
} diagnostics_state_t;


extern diagnostics_state_t diagnostics;

#ifdef __cplusplus
}
#endif

#endif
