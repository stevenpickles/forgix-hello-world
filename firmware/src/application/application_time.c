/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "application_time.h"




/***************************************************************************************
**
** Public Function Definitions
**
***************************************************************************************/


/// <summary>
///     Compares by signed difference rather than by magnitude, which is what
///     makes the millisecond clock's 49-day rollover a non-event: a plain
///     now >= deadline would answer "not yet" for half the counter's range once
///     it has wrapped, stalling every timer in the firmware at once. A deadline
///     exactly reached counts as due.
/// </summary>
/// <returns>
///     True once now is at or past the deadline, wrap included.
/// </returns>
bool application_deadline_reached( const uint32_t now_ms, const uint32_t deadline_ms )
{
    return (int32_t) ( now_ms - deadline_ms ) >= 0;
}

/// <summary>
///     Elapsed-time test in the same wrap-safe signed form. The threshold is
///     cast to signed as well, so it has to stay well under 2^31 ms; every
///     threshold this serves is seconds, not days.
/// </summary>
/// <returns>
///     True once at least threshold_ms has elapsed since since_ms.
/// </returns>
bool application_stalled_since( const uint32_t now_ms, const uint32_t since_ms,
                                const uint32_t threshold_ms )
{
    return (int32_t) ( now_ms - since_ms ) >= (int32_t) threshold_ms;
}
