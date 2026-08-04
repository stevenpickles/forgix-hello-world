#!/usr/bin/env python3
"""Decode the diagnostics scratch registers read out of a Forgix board.

The USB-free image has no console, so when the LED stops there is no way to ask
it anything. These registers survive a watchdog reset and can be read over SWD
from a board that is still frozen, which is the only route to the evidence.

Read them with the board still powered, for example:

    openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \\
        -c "init; halt; mdw 0x400d8000 8; mdw 0x4010002c 1; shutdown"

then pass the four scratch words (and optionally REASON, CTRL, CHIP_RESET):

    python scripts/decode_scratch.py 0x6 0x1c2 0x0 0x48000000
"""

from __future__ import annotations

import argparse

MARKERS = {
    0: "none (never set)",
    1: "LOOP - foreground loop body",
    2: "CONSOLE_READ - blocked reading the console",
    3: "CONSOLE_WRITE - blocked in a console write (untimed stdio flush)",
    4: "COMMAND - dispatching a shell command",
    5: "USB_SNAPSHOT - sampling USB health",
    6: "FPGA_CHECK - CDONE, design-ID ping, LED register readback",
    7: "MENU - drawing or dispatching the front-panel menu",
    8: "IBIT - running a built-in test step",
    9: "EFFECT - painting the blinker or the advanced blinker",
    # Not a code path: the built-in test writes this, reads it straight back to
    # prove the register holds, and replaces it. Landing here means a reset
    # caught a window a few microseconds wide.
    0x5A5A5A5A: "SELF_TEST_PATTERN - inside the built-in test's marker round trip",
}

WATCHDOG_REASON_TIMER = 1 << 0
WATCHDOG_REASON_FORCE = 1 << 1
WATCHDOG_CTRL_ENABLE = 1 << 30
POWMAN_HAD_BOR = 1 << 17
POWMAN_HAD_POR = 1 << 16


def word(text: str) -> int:
    value = int(text, 0)
    if not 0 <= value <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError(f"not a 32-bit word: {text}")
    return value


def describe_uptime(seconds: int) -> str:
    if seconds == 0:
        return "0 s - froze before the first one-second sample"
    return f"{seconds} s ({seconds // 60} min {seconds % 60} s) of foreground progress"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scratch0", type=word, help="0x400d800c - progress marker")
    parser.add_argument("scratch1", type=word, help="0x400d8010 - loop-seconds counter")
    parser.add_argument("scratch2", type=word, help="0x400d8014 - CDC activity counter")
    parser.add_argument("scratch3", type=word, help="0x400d8018 - packed health word")
    parser.add_argument("--reason", type=word, help="0x400d8008 - WATCHDOG_REASON")
    parser.add_argument("--ctrl", type=word, help="0x400d8000 - WATCHDOG_CTRL")
    parser.add_argument("--chip-reset", type=word, help="0x4010002c - POWMAN_CHIP_RESET")
    arguments = parser.parse_args()

    print("Forgix diagnostics scratch")
    print(f"  marker        {arguments.scratch0:#010x}  "
          f"{MARKERS.get(arguments.scratch0, 'unrecognized')}")
    print(f"  uptime        {arguments.scratch1:#010x}  {describe_uptime(arguments.scratch1)}")
    print(f"  cdc activity  {arguments.scratch2:#010x}  "
          f"{arguments.scratch2} completed transfers (always 0 in the USB-free image)")

    health = arguments.scratch3
    frame = health & 0xFFFF
    connected = bool(health & (1 << 16))
    suspended = bool(health & (1 << 17))
    write_blocked = bool(health & (1 << 18))
    fpga_failures = (health >> 19) & 0x7F
    fpga_reconfigures = (health >> 26) & 0x3F

    print(f"  health        {health:#010x}")
    print(f"    start-of-frame      {frame}")
    print(f"    DTR asserted        {connected}")
    print(f"    bus suspended       {suspended}")
    print(f"    tx FIFO full        {write_blocked}")
    print(f"    FPGA failures       {fpga_failures}  (modulo 128)")
    print(f"    FPGA reconfigures   {fpga_reconfigures}  (modulo 64)")

    if arguments.ctrl is not None:
        armed = bool(arguments.ctrl & WATCHDOG_CTRL_ENABLE)
        print(f"  watchdog ctrl {arguments.ctrl:#010x}  {'armed' if armed else 'NOT ARMED'}")
    if arguments.reason is not None:
        causes = []
        if arguments.reason & WATCHDOG_REASON_TIMER:
            causes.append("timer expiry")
        if arguments.reason & WATCHDOG_REASON_FORCE:
            causes.append("forced")
        print(f"  wd reason     {arguments.reason:#010x}  "
              f"{', '.join(causes) if causes else 'no watchdog reset recorded'}")
    if arguments.chip_reset is not None:
        flags = []
        if arguments.chip_reset & POWMAN_HAD_BOR:
            flags.append("brownout")
        if arguments.chip_reset & POWMAN_HAD_POR:
            flags.append("power-on")
        print(f"  chip reset    {arguments.chip_reset:#010x}  "
              f"{', '.join(flags) if flags else 'neither BOR nor POR'}")

    print("\nReading")
    if fpga_reconfigures:
        print("  The FPGA was reconfigured at least once, so the MCU was alive and the")
        print("  FPGA had lost its configuration. That is mode 2 attributed.")
    elif fpga_failures:
        print("  The FPGA check failed but reconfiguration never succeeded. The fault")
        print("  survives a full bitstream reload - look at power, the oscillator on")
        print("  GPIO 19, or thermal, not at configuration memory.")
    else:
        print("  The FPGA check never failed. If the LED was dark, the FPGA was still")
        print("  answering on the bus and only its LED output stopped, or the freeze")
        print("  happened between samples.")

    if arguments.scratch0 == 6:
        print("  Marker FPGA_CHECK: the foreground was inside the FPGA health check.")
    elif arguments.scratch0 == 1:
        print("  Marker LOOP: the foreground stopped in the loop body, away from the")
        print("  console and FPGA paths.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
