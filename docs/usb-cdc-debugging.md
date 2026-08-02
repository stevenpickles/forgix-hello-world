# Firmware lockup debugging plan (USB CDC and USB-free)

## Purpose and terminology

This plan covers **two distinct failure modes** on the Forgix board:

1. **USB shell hang, ~9-10 min.** A repeatable loss of the Forgix USB serial
   session on a Windows 11 host.
2. **USB-free LED hang, ~45-75 min.** The image with USB compiled out also
   freezes, on every power source tried.

The document originally scoped only the first. The second was discovered later
and invalidated this plan's variant-A control result; see
[Evidence collected so far](#evidence-collected-so-far). Both are now tracked
here, and the instrumented-firmware approach that supersedes much of the serial
ladder below is specified in
[the lockup investigation plan](lockup-investigation-plan.md).

USB-C is the physical connector. USB Communications Device Class, using its
Abstract Control Model (CDC-ACM), is the protocol that makes the RP2354
firmware appear as a Windows COM port.

The relevant path is:

```text
Forgix application
  -> BSP console and Pico SDK stdio
  -> TinyUSB CDC-ACM device
  -> RP2354 USB controller and USB-C connection
  -> Windows usbser.sys
  -> COM3 client
```

The experiments change one layer at a time. A passing result removes a
hypothesis only for the exact conditions tested.

## Evidence collected so far

The following observations were made with Pico SDK 2.3.0, its TinyUSB 0.18.0
submodule, and a Forgix connected to a Windows 11 host as `COM3`:

- The application shell operated normally after boot and then stopped
  responding after approximately nine to ten minutes in three runs.
- Quiet output did not prevent the failure. The last typed `s` was visible at
  the host interface but was not echoed by the firmware.
- A temporary diagnostic emitted `hello world - <n>` once per second and drove
  the FPGA RGB LED at 2 Hz. The last serial heartbeat was near 475 seconds, and
  the foreground-driven LED stopped with it.
- Windows continued to list the COM port after that failure. This is consistent
  with a configured device whose data path or firmware has stopped progressing;
  it is not evidence by itself that the Windows driver is responsible.
- A diagnostic image with USB support and stdio initialization compiled out
  drove the same FPGA LED for more than 45 minutes while powered through the
  same PC connection.
- **Correction (2026-08-01).** Running that USB-free image for longer showed it
  also freezes, after roughly 45 to 75 minutes, on a USB-C wall charger, a PC
  port, **and** a USB battery pack. The LED stops (lit or dark) and a power
  cycle recovers it. The ">45 min pass" above was simply not a long enough run.

That correction changes the shape of the investigation. The USB-free result is
no longer a clean control, so it does **not** establish that the 9-10 minute
failure is USB-specific; it only establishes that the two failures have
different time constants. Occurring on three independent power sources, the
second failure is board-local rather than host-driven.

Because the RGB LED is driven by the **FPGA**, a frozen LED is ambiguous on its
own: it is consistent with an MCU hang *and* with the FPGA losing configuration
or its clock while the MCU keeps running. Distinguishing those two required
instrumentation, which is why the firmware now carries a watchdog, retained
progress markers, and a runtime FPGA health check.

There is no documented fixed nine- or ten-minute RP2350 USB limit. The RP2350
datasheet does document USB status synchronization erratum RP2350-E12 and
requires `clk_sys` to remain at least 10% faster than `clk_usb` while USB is
active. The normal 150 MHz system clock and 48 MHz USB clock satisfy that
workaround. TinyUSB has also fixed a Raspberry Pi device-controller state
machine hang in the past; the corresponding workaround is enabled in the
version used by this project. These are reasons to isolate the stack carefully,
not proof that either issue explains this failure.

References:

- [RP2350 datasheet and errata](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
- [TinyUSB Raspberry Pi device-mode hang workaround](https://github.com/hathach/tinyusb/pull/1779)
- [Microsoft USBView documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/usbview)
- [Microsoft USB ETW debugging guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/best-practices--debugging-usb-device-problems)

## Test controls

Use the same board, known-good cable, FPGA image, firmware toolchain, and direct
PC port until a test specifically changes one of them. Avoid a hub. Keep the PC
awake and do not change Windows power settings during the initial firmware
ladder.

Only one process may own `COM3`. Completely close PuTTY, IO Ninja, PowerShell
serial scripts, and other terminal programs unless the test explicitly calls
for one. Record the following for every run:

- Firmware variant and Git commit.
- USB port and cable.
- Whether a client opened `COM3`, including the client name and DTR/RTS policy.
- Start time, last successful input, last successful output, and LED behavior.
- Whether `COM3` remained present and whether reopening it recovered service.
- Whether `picotool reboot -f -u` worked after the failure.

Use 20 minutes as the initial screen because it is at least twice the common
failure window. A passing variant should subsequently complete two 30-minute
runs before it is considered cleared.

## Stage 1: confirm the existing boundary

1. Run the USB-disabled LED image from a power-only USB-C cable and AC adapter.
   Use at least two hours, not 30 minutes: this image is now known to fail in
   the 45-75 minute window, so a short run reports a false pass. Use the
   instrumented `forgix_led_only_diagnostic` image so the outcome is
   attributable.
2. Run the pre-diagnostic application firmware from the PC with every COM-port
   client closed for 20 minutes.
3. Repeat with one simple client opening `COM3` but sending no data.

The first test completes the independent-power control. The second determines
whether enumeration and background USB activity can trigger the failure without
a terminal session. The third adds only the CDC open and control-line state.

## Stage 2: firmware functionality ladder

Build small diagnostic targets that retain the visible 2 Hz LED heartbeat but
enable only the listed USB behavior:

| Variant | Device behavior | Host behavior | Isolates |
| --- | --- | --- | --- |
| A | USB compiled out | No client | **Not a baseline.** This variant fails on its own at 45-75 min; it is the second failure mode, now instrumented |
| B | Initialize and enumerate; never read or write | No client | Device stack and Windows enumeration |
| C | Same as B | Open `COM3`, then remain idle | CDC open, close, and control requests |
| D | Transmit a numbered line once per second; never read | Open and continuously read | Device-to-host transfers and TX backpressure |
| E | Poll for input; never transmit | Send a numbered character periodically | Host-to-device transfers and RX polling |
| F | Receive and echo; no unsolicited status | Send a numbered sequence and verify it | Bidirectional CDC interaction |
| G | Echo plus one numbered status per second | Exercise input and verify both streams | Expected traffic without the command parser |
| H | Complete application shell | Run the automated interaction | End-to-end confirmation |

Stop at the first failing transition and reproduce it once before adding more
functionality. Also run the immediately preceding passing variant again under
the same host conditions. This guards against treating an incidental cable,
port, or host event as a firmware boundary.

## Stage 3: make a stall observable (implemented)

This stage is now built into the firmware. Both images run the same
`application_diagnostics` loop; the decision trees that turn its output into a
verdict live in [the lockup investigation plan](lockup-investigation-plan.md).

Keep the instrumented images out of pure boundary tests: the watchdog converts
a hang into a reset, which changes the failure symptom.

### Images and build knobs

| Image | Build | Purpose |
| --- | --- | --- |
| `forgix_hello_world.uf2` | default | Full shell with USB CDC and diagnostics |
| `forgix_led_only_diagnostic.uf2` | default | USB compiled out; LED and FPGA instrumentation only |

Both are produced by `./scripts/build_firmware.sh`. Load either with
`./scripts/flash.sh <image-name>`, defaulting to `forgix_hello_world`.

`cmake -DFORGIX_FOREGROUND_USB_SERVICE=ON` moves TinyUSB servicing into the
foreground loop and disables the SDK's background IRQ task, which removes the
alarm-pool dependency suspected in the mode-1 hypotheses. Never enable one
without the other; two owners of `tud_task()` corrupt stack state.

### Progress markers

Written to watchdog `scratch[0]` before entering each path, so a watchdog reset
names where the foreground stopped.

| Value | Marker | Set before |
| ---: | --- | --- |
| 1 | `LOOP` | Each foreground iteration, and after each completed sample |
| 2 | `CONSOLE_READ` | `bsp_console_getchar_timeout_us` |
| 3 | `CONSOLE_WRITE` | Every console write, all of which reach the SDK's untimed stdio flush |
| 4 | `COMMAND` | Command dispatch |
| 5 | `USB_SNAPSHOT` | Reading USB health |
| 6 | `FPGA_CHECK` | CDONE, design-ID ping, and LED register readback |

### Retained scratch layout

`scratch[0..3]` survive a watchdog reset; the SDK reserves `scratch[4..7]`.

| Register | Contents |
| --- | --- |
| `scratch[0]` | Progress marker (table above) |
| `scratch[1]` | Loop-seconds counter (uptime in whole seconds) |
| `scratch[2]` | CDC activity counter (completed transfers) |
| `scratch[3]` | Packed health word (below) |

Packed health word, `scratch[3]`:

| Bits | Field |
| --- | --- |
| 0-15 | Host start-of-frame number |
| 16 | DTR asserted |
| 17 | Bus suspended |
| 18 | Transmit FIFO full |
| 19-25 | FPGA failure count (modulo 128) |
| 26-31 | FPGA reconfiguration count (modulo 64) |

### Boot report

Emitted before the watchdog is armed. In the USB image it is a serial line:

```text
diag: boot=watchdog marker=3 loop=612 usb=44 health=00010001
```

`boot=` is one of `power-on`, `brownout`, `watchdog`, or `other`; `brownout`
comes from the RP2350 POWMAN chip-reset register and is the direct test for a
supply droop. The remaining fields are the retained scratch registers from
before the reset. The `diag` shell command prints the same line plus live
counters at any time.

The USB-free image has no console and blinks the same report instead. The code
plays three times, because it cannot be replayed on demand: power-cycling to
watch it again resets the scratch registers it is reporting.

| Blink code | Meaning | Resting heartbeat |
| --- | --- | --- |
| White x1 | Clean power-on | Blue |
| Yellow x2 | Brownout reset | Yellow |
| Cyan x3 | Unclassified reset | Cyan |
| Red x N | Watchdog reset; N is the retained marker value, clamped to 1-8 | Red |

The resting color persists for the whole run, so the verdict stays readable
hours later even if the blink code was missed. **Blue is nominal; any other
resting color means something happened.** Only the blink count carries the
marker, so read it while it plays if the heartbeat comes up red.

Expect roughly 1.7 s of darkness after power-up while the FPGA is configured and
settles, then the blink code, then the heartbeat.

### Live LED colors

The 2 Hz heartbeat carries current health without needing a reset. These
override the visual effect of `color` and `off` for the duration of the
investigation; the commands still answer `ok`, so `test_hardware.ps1` passes.

| Color | Meaning |
| --- | --- |
| Green | Connected and CDC transfers are completing |
| Red | Connected, but no transfer has completed for 5 s: endpoint or stack wedge |
| Magenta | Bus suspended, or start-of-frame frozen for 5 s |
| Blue | Host has not asserted DTR |
| White x3 | FPGA was reconfigured and recovered |

In the USB-free image there is no USB health to show, so the resting color
reports the last boot reason instead, per the table above.

The white recovery signature is the decisive observation for the second failure
mode. The LED is FPGA-driven, so a freeze that ends with three white flashes and
a growing reconfiguration count proves the MCU stayed alive and the FPGA lost
its configuration -- a distinction that otherwise needs instruments on the board.

### Running a soak

```powershell
./scripts/soak_serial.ps1 -Port COM3 -DurationMinutes 20
./scripts/soak_serial.ps1 -Port COM3 -DurationMinutes 120 -SendIntervalSeconds 10
```

This one script stays PowerShell: serial from Git Bash does not work on the
tested machine. `/dev/ttyS2` and `/dev/com3` are real character devices and open
with `clocal` set, but carry no data, while .NET `SerialPort` on the same port
works. A bash conversion needs `pyserial`, which is not installed.

It opens the port once and never reopens it, because the single clean reopen is
itself the experiment that separates a wedged device from a recoverable host
session. Use `-ValidateOnly` to check parameters with no board attached.

The USB-free image needs no harness: it needs only a power source and a note of
the wall-clock time at which the LED froze and, if it happens, recovered.

### Relationship to the ladder below

Where the decision trees resolve a question, the corresponding serial variants
in Stage 2 are redundant. The instrumented images subsume variants B through G:
the markers, health snapshot, and LED colors report which layer stopped without
requiring a separate build per layer. Fall back to the ladder only for a
surviving ambiguity, or if an instrumented image stops reproducing the failure.

## Stage 4: capture the Windows side

Use a PowerShell harness instead of a graphical terminal for the transfer tests.
It should timestamp connection attempts, DTR/RTS state, every numbered message,
echo latency, gaps, read/write exceptions, and the last successful operation.
It should not automatically reopen the port or reset the board after a failure.

Install USBView from the Windows SDK Debugging Tools and save the Forgix
descriptor and topology information before testing. At a failure, do not unplug
the board immediately:

1. Record the exact time, LED state, and last sequence number.
2. Check whether Device Manager and USBView still show the device and its CDC
   interface.
3. Save relevant Windows USB ETW and device events.
4. Close the original client and attempt one clean reopen of `COM3`.
5. Attempt `picotool reboot -f -u` only after the passive observations, because
   it deliberately changes the device state.
6. Power-cycle only after the preceding evidence is collected.

The result distinguishes a missing or re-enumerated device, a configured device
with an unresponsive CDC interface, and a client-session problem recoverable by
closing and reopening the port.

## Stage 5: vary the host after finding the boundary

Once the first failing firmware transition is known, repeat that exact image
and traffic pattern with one host-side change at a time:

1. Use a rear motherboard USB port without a hub or front-panel connection.
2. Use a port on a different Windows USB controller, if available.
3. Compare the PowerShell harness with PuTTY and IO Ninja, one at a time.
4. Temporarily disable USB selective suspend for one controlled run, then
   restore it. Microsoft recommends leaving selective suspend enabled normally.
5. Run the same image and traffic pattern from a Linux host or another Windows
   11 computer.

A Windows-only failure directs attention to `usbser.sys`, CDC control requests,
client control-line policy, and Windows power management. A failure at the same
firmware boundary on multiple operating systems directs attention to application
stdio usage, Pico SDK/TinyUSB, or the RP2354 USB peripheral.

## Results log

Append results as runs are executed. Record the LED color or blink code and the
`diag:` boot report, not only pass or fail: those are what the decision trees
consume. For the USB-free image, record the exact wall-clock time to freeze --
a cluster near 71.6 minutes would implicate a 32-bit microsecond wrap.

| Date | Commit / variant | Host and port | Client state | Duration | LED | COM port | Recovery | Result |
| --- | --- | --- | --- | ---: | --- | --- | --- | --- |
| 2026-08-01 | A, USB disabled | Windows 11 PC | Closed | >45 min | Continued at 2 Hz | Not enumerated by design | Not needed | ~~Pass~~ **Superseded** |
| 2026-08-01 | A, USB disabled | Wall charger, PC port, and battery pack | Closed | 45-75 min | Froze (lit or dark) | Not enumerated by design | Power cycle | **Fail** - invalidates the row above, which was too short to reach the failure |
| 2026-08-02 | Instrumented USB shell, `64b60a8` | Windows 11 PC, direct port | Soak harness, DTR on, `diag` ping every 30 s | ~4 min | n/a | **Vanished, then RP2350 Boot + Mass Storage** | Power cycle | **Fail** - bootrom fell into USB boot; `picotool save` read flash back as all `0x00` |
| 2026-08-02 | Instrumented USB shell, `64b60a8` | Windows 11 PC, direct port | Soak harness, DTR on, `diag` ping every 30 s | **8 min 40 s** (boot 11:46:42, gone 11:55:22) | n/a | **Vanished entirely, no BOOTSEL** | Power cycle | **Fail** - healthy at t=485 s (`connected=1 activity=391 sof=1865 fpga_fail=0`), off the bus 35 s later with no watchdog re-enumeration |
| 2026-08-02 | Instrumented USB shell, `64b60a8` | Windows 11 PC, direct port | Soak harness, DTR on, `diag` ping every 30 s | **9 min 14 s** (boot 11:59:10, gone 12:08:24) | n/a | **Vanished entirely** | Power cycle | **Fail** - healthy at t=539 s (`connected=1 activity=365 sof=861 fpga_fail=0`). Two consecutive runs at 8.7 and 9.2 min reproduce the original 9-10 min window precisely |
| 2026-08-02 | USB shell **with QSPI CS1 deselect**, `5544dba` | Windows 11 PC, direct port | Soak harness, DTR on, `diag` ping every 30 s | **120 min (full run)** | Green/red per instrumentation defect below, board healthy | Present throughout | Not needed | **PASS** - 2182 lines, all 239 pings answered, largest gap 10.1 s (the normal idle cadence), uptime monotonic 61 s to 7208 s across 239 samples with **zero resets**. 13x the longest failing baseline |
