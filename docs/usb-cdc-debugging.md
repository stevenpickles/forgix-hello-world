# USB CDC communications debugging plan

## Purpose and terminology

This plan isolates a repeatable loss of the Forgix USB serial session on a
Windows 11 host. USB-C is the physical connector. USB Communications Device
Class, using its Abstract Control Model (CDC-ACM), is the protocol that makes
the RP2354 firmware appear as a Windows COM port.

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

The USB-disabled result demonstrates sustained RP2354 execution, FPGA
configuration, SPI register access, LED control, and PC-supplied power beyond
the usual failure window. The remaining evidence points to functionality
activated by USB, but it does not yet identify whether the foreground is
blocked by application stdio, the device stack is stalled, or the Windows host
is no longer completing transfers.

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

1. Run the USB-disabled LED image for 30 minutes from a power-only USB-C cable
   and AC adapter.
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
| A | USB compiled out | No client | Established non-USB baseline |
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

## Stage 3: make a stall observable

Add a watchdog that is fed only after one complete foreground-loop iteration.
Store a small progress marker before the important operations: FPGA/LED update,
USB receive poll, USB transmit, status formatting, and command processing. On a
watchdog reboot, report the reset cause and last marker before resuming the test.

Interpret the visible behavior as follows:

- LED stops and the watchdog restarts the image: the foreground stopped making
  progress.
- LED continues while CDC traffic stops: the failure is isolated below the
  application loop or within the USB data path.
- The device disappears and returns with a boot indication: investigate a reset,
  disconnect, or power event rather than a permanently blocked call.

Keep the watchdog diagnostic separate from the initial boundary test because a
periodic reset changes the failure symptom.

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

Append results as the ladder is executed:

| Date | Commit / variant | Host and port | Client state | Duration | LED | COM port | Recovery | Result |
| --- | --- | --- | --- | ---: | --- | --- | --- | --- |
| 2026-08-01 | A, USB disabled | Windows 11 PC | Closed | >45 min | Continued at 2 Hz | Not enumerated by design | Not needed | Pass |
