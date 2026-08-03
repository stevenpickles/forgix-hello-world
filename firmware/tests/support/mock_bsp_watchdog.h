#ifndef FORGIX_MOCK_BSP_WATCHDOG_H
#define FORGIX_MOCK_BSP_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_watchdog.h"

enum { MOCK_BSP_WATCHDOG_MARKER_HISTORY = 64 };

void mock_bsp_watchdog_reset(void);

/* Retained state a reset would have left behind, read by the boot report. */
void mock_bsp_watchdog_set_boot_reason(bsp_boot_reason_t reason);
void mock_bsp_watchdog_set_retained(uint32_t marker, uint32_t slot0, uint32_t slot1,
                                    uint32_t slot2);

bool mock_bsp_watchdog_started(void);
uint32_t mock_bsp_watchdog_timeout_ms(void);
uint32_t mock_bsp_watchdog_feed_count(void);
uint32_t mock_bsp_watchdog_marker(void);
uint32_t mock_bsp_watchdog_snapshot(uint32_t slot);

/* Ordered record of every marker write, so tests can assert that a code path was
   marked before it ran rather than only that the final marker is correct. */
uint32_t mock_bsp_watchdog_marker_writes(void);
uint32_t mock_bsp_watchdog_marker_at(uint32_t index);
bool mock_bsp_watchdog_marker_was_written(uint32_t marker);

#endif
