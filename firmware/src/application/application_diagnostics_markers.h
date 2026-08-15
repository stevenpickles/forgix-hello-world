#ifndef FORGIX_APPLICATION_DIAGNOSTICS_MARKERS_H
#define FORGIX_APPLICATION_DIAGNOSTICS_MARKERS_H

#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Enumerated Values, Type Definitions
**
***************************************************************************************/


/* Progress markers written to the watchdog marker register. After a watchdog
   reset the retained value names the code path that stopped making progress.

   These sit in a header of their own because every module in the foreground
   loop writes one, while almost none of them has any other business with the
   diagnostics module. application_diagnostics.h includes this, so a consumer
   that wants both still needs only the one include it already had. */
enum
{
    APPLICATION_DIAGNOSTICS_MARKER_LOOP = 1,
    APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_READ = 2,
    APPLICATION_DIAGNOSTICS_MARKER_CONSOLE_WRITE = 3,
    APPLICATION_DIAGNOSTICS_MARKER_COMMAND = 4,
    APPLICATION_DIAGNOSTICS_MARKER_USB_SNAPSHOT = 5,
    APPLICATION_DIAGNOSTICS_MARKER_FPGA_CHECK = 6,
    /* The menu and the built-in test are the newest code in the foreground loop
       and so the likeliest to stall it. Without their own markers a watchdog
       reset from either would be attributed to whatever ran last instead. */
    APPLICATION_DIAGNOSTICS_MARKER_MENU = 7,
    APPLICATION_DIAGNOSTICS_MARKER_IBIT = 8,
    APPLICATION_DIAGNOSTICS_MARKER_EFFECT = 9,
    /* One ordinary mapped-QPI slice of the destructive PSRAM test. */
    APPLICATION_DIAGNOSTICS_MARKER_MEMTEST = 10,
    /* Not a code path. Written and read straight back by the built-in test's
       watchdog step to prove the scratch register holds a value, then replaced
       by MARKER_IBIT. A reset caught inside that window -- a few microseconds --
       leaves this behind, which is why it is listed rather than left to decode
       as an unrecognised number.

       0x5A5A5A5A rather than its complement because an enumerator has to fit in
       an int, and -pedantic is right to say so. */
    APPLICATION_DIAGNOSTICS_MARKER_SELF_TEST_PATTERN = 0x5A5A5A5A,
};

#ifdef __cplusplus
}
#endif

#endif
