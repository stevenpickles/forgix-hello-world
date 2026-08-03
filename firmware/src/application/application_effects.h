#ifndef FORGIX_APPLICATION_EFFECTS_H
#define FORGIX_APPLICATION_EFFECTS_H

#include "application_ui.h"

/* Never ends on its own; the classic blinky, one colour at a time at 1 Hz. It is
   the first thing anyone reaches for on a new board and the last thing worth
   removing, because a blink that keeps time proves the loop is still running
   long after a one-shot test has stopped saying anything. */
const application_activity_t *application_effects_blinker(void);

/* A finite show: heartbeat, colour wheel, aurora, then back to the menu. These
   are the effects scripts/test_hardware.ps1 drove from the host; running them in
   firmware means a board with no host attached can still be looked at. */
const application_activity_t *application_effects_advanced(void);

#endif
