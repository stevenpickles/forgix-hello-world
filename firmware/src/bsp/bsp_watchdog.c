/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_watchdog.h"

#include "hardware/structs/powman.h"
#include "hardware/watchdog.h"




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


/* The SDK owns scratch[4..7] for its own reboot bookkeeping, so the diagnostics
   layer is confined to scratch[0] (progress marker) and scratch[1..3]
   (BSP_WATCHDOG_SNAPSHOT_SLOTS health snapshots). Those registers survive a
   watchdog reset, which is what makes a hang self-attributing. */

/* Indices into watchdog_hw->scratch naming the two regions described above:
   the single progress marker slot and the first of the contiguous snapshot
   slots that follow it. */
typedef enum bsp_watchdog_scratch_register_tag
{
    MARKER_REGISTER = 0,
    SNAPSHOT_BASE_REGISTER = 1
} bsp_watchdog_scratch_register;




/***************************************************************************************
**
** Private Variable Declarations
**
***************************************************************************************/


/* watchdog_enable writes the very scratch word watchdog_enable_caused_reboot
   consults, so once BSP_WatchdogStart has armed, the hardware answers "the
   watchdog did it" on every board. The reason is therefore latched on the
   first query and served from here ever after. */
static bool _bootReasonLatched;
static bsp_boot_reason _bootReason;




/***************************************************************************************
**
** Private Function Declarations
**
***************************************************************************************/


static bsp_boot_reason _ClassifyBootReason( void );




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Arms the hardware watchdog. Irreversible: once started it must be fed
///     within every window or the chip resets, so nothing that blocks for
///     longer than the timeout may run after this. Arming also poisons the
///     hardware's record of why the last boot happened, which is why the boot
///     reason below is latched rather than re-read.
/// </summary>
void BSP_WatchdogStart( const uint32_t timeoutMs )
{
    watchdog_enable( timeoutMs, true );
}

/// <summary>
///     Restarts the timeout window. Called from one place in the foreground
///     loop on purpose -- feeding from several would let a hung path stay alive
///     because some other path kept feeding for it.
/// </summary>
void BSP_WatchdogFeed( void )
{
    watchdog_update();
}

/// <summary>
///     Reports why the last boot happened, classified from the hardware once
///     and latched: watchdog_enable overwrites the flag the classification
///     reads, so a query made after BSP_WatchdogStart would blame the watchdog
///     on every boot of every board. Latching on first call rather than in
///     Start also keeps the answer honest for a caller that never arms.
/// </summary>
/// <returns>
///     The cause as it stood before the watchdog was armed, or BSP_BOOT_OTHER
///     when none of the known bits explain it.
/// </returns>
bsp_boot_reason BSP_WatchdogBootReason( void )
{
    if ( !_bootReasonLatched )
    {
        _bootReason = _ClassifyBootReason();
        _bootReasonLatched = true;
    }
    return _bootReason;
}

/// <summary>
///     Records where the foreground loop has reached, in a scratch register that
///     survives a watchdog reset. This is what makes a hang self-attributing:
///     after the reboot the marker still names the code that was running.
/// </summary>
void BSP_WatchdogMarkerSet( const uint32_t marker )
{
    watchdog_hw->scratch[ MARKER_REGISTER ] = marker;
}

/// <summary>
///     Reads back the marker left by the previous boot. Meaningful only before
///     the current boot overwrites it, so the boot report reads it first.
/// </summary>
/// <returns>
///     The last marker written, from before the reset if one occurred.
/// </returns>
uint32_t BSP_WatchdogMarkerGet( void )
{
    return watchdog_hw->scratch[ MARKER_REGISTER ];
}

/// <summary>
///     Stores a health value in one of the retained slots. Out-of-range slots
///     are dropped rather than trapped, because the SDK owns the scratch
///     registers past this range and writing into them would corrupt its own
///     reboot bookkeeping.
/// </summary>
void BSP_WatchdogSnapshotSet( const uint32_t slot, const uint32_t value )
{
    if ( slot < BSP_WATCHDOG_SNAPSHOT_SLOTS )
    {
        watchdog_hw->scratch[ SNAPSHOT_BASE_REGISTER + slot ] = value;
    }
}

/// <summary>
///     Reads a retained slot from before the last reset. Zero is returned both
///     for an out-of-range slot and for a slot that was genuinely zero, so it
///     cannot be used to tell "absent" from "was zero".
/// </summary>
/// <returns>
///     The retained value, or zero.
/// </returns>
uint32_t BSP_WatchdogSnapshotGet( const uint32_t slot )
{
    if ( slot < BSP_WATCHDOG_SNAPSHOT_SLOTS )
    {
        return watchdog_hw->scratch[ SNAPSHOT_BASE_REGISTER + slot ];
    }
    return 0;
}




/***************************************************************************************
**
** Private Function Definitions
**
***************************************************************************************/


/// <summary>
///     Classifies the boot cause from the hardware, checking the watchdog flag
///     before the power-on and brownout bits: a watchdog reset can leave those
///     bits set too, so testing them first would misattribute a hang to a
///     power event. Valid only before the watchdog is armed, which is why the
///     public query latches this answer.
/// </summary>
/// <returns>
///     The cause, or BSP_BOOT_OTHER when none of the known bits explain it.
/// </returns>
static bsp_boot_reason _ClassifyBootReason( void )
{
    if ( watchdog_enable_caused_reboot() )
    {
        return BSP_BOOT_WATCHDOG;
    }

    const uint32_t chip_reset = powman_hw->chip_reset;
    if ( chip_reset & POWMAN_CHIP_RESET_HAD_BOR_BITS )
    {
        return BSP_BOOT_BROWNOUT;
    }
    if ( chip_reset & POWMAN_CHIP_RESET_HAD_POR_BITS )
    {
        return BSP_BOOT_POWER_ON;
    }
    return BSP_BOOT_OTHER;
}
