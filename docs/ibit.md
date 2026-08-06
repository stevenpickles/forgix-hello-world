# Initiated built-in test

What the board checks about itself when you press `1` at the menu, what each
verdict means, and what it deliberately does not check.

## Getting there

Power the board and open the serial port at 115200 baud with terminal local echo
off. Whenever you get there, the board is repeating:

```text
hello world - 47 - press any key
```

The count is seconds since boot, not bytes sent, so it keeps advancing while
nothing is listening. Reading `47` means the board has been up and transmitting
for the three quarters of a minute it took you to find the port. That is the
whole purpose of the banner: it proves the board can transmit and the host can
receive **without needing the host to be able to transmit**. If the count is
advancing, the assembly, the USB link and the foreground loop are all alive, and
anything that is wrong is downstream of that.

Any key opens the menu. The key is consumed by opening the menu and is not also
treated as a selection.

## The five outcomes

| | Meaning |
|---|---|
| `PASS` | Checked and correct |
| `FAIL` | Checked and wrong. Something is broken |
| `TIMEOUT` | The step ran but a person did not act. Not a fault |
| `SKIP` | A prerequisite failed, so the result would not have meant anything |
| `INFO` | Measured and reported; there is no correct value to compare against |

`TIMEOUT` and `SKIP` are kept out of `FAIL` on purpose. An unattended run
finishes with one `TIMEOUT` and that is a healthy board. A board with a dead FPGA
reports one `FAIL` and three `SKIP`, rather than four failures that would bury
which one is the actual fault.

## The steps

| # | Step | Passes when | A failure means |
|---|---|---|---|
| 1 | Chip identity | `SYSINFO CHIP_ID` reads manufacturer `0x493`, part `0x0004` | Not an RP2350-family part, or SYSINFO is unreadable |
| 2 | Board identity | The 64-bit unique ID is neither all-zero nor all-ones | The flash die did not answer the unique-ID command |
| 3 | Clocks | Measured `clk_sys` 150 MHz and `clk_usb` 48 MHz within 1%, and `clk_sys` ≥ 1.1 × `clk_usb` | A PLL did not lock, or the RP2350-E12 margin is violated and USB status synchronisation is unreliable |
| 4 | Memory sizing | Flash 2048 KiB, SRAM 520 KiB | The image was linked for a different part |
| 5 | OTP flash device info | *Always `INFO`* | — |
| 6 | Boot flash | Readable, reset vector sane | The QSPI bus or the boot die is faulty |
| 7 | QSPI PSRAM | A moving-inversion sweep over the full 2 MiB — an address-derived pattern, then its inverse, written and verified chunk by chunk through the uncached window — survives | A bad cell or address line on the chip-select-1 device, or the bus it shares with the boot flash |
| 8 | Die temperature | Between −20 °C and +85 °C | The ADC or its reference is dead. A reading pinned at a rail is the fault worth catching; the absolute figure is several degrees out uncalibrated |
| 9 | USB link | DTR asserted, not suspended, the host's start-of-frame counter advanced between two samples 20 ms apart, transmit FIFO not full | The host stopped framing, or the transmit path is backed up |
| 10 | Watchdog and boot reason | The scratch marker round-trips, and the previous boot was not a watchdog reset | A prior watchdog reset means something stopped feeding the loop; the retained marker names where. A failed round-trip means the register the whole diagnosis rests on does not hold |
| 11 | FPGA configuration | `CDONE` high and a ping returns `0xB6` | The bitstream did not load, or the design is not the expected one |
| 12 | FPGA register bus | A walking pattern `5A A5 3C C3` written to the LED registers reads back byte for byte | The three-wire runtime link. `0x00` and `0xFF` are deliberately not used: they are what a bus stuck low or high returns |
| 13 | RGB LED | Red, green, blue, white and off each read back from the FPGA | A colour channel, or the register path to it |
| 14 | Button SW1 | The debounced level **and** the press counter both change within 15 s | Either alone could be a stuck event line or a pin held low; requiring both separates a real press from a fault that resembles one |
| 15 | FPGA 32 MHz clock | Two latched samples of the FPGA's free-running counter, 500 ms apart, advance at 32 MHz within 1% | The oscillator is off frequency, or it stalled and recovered inside the window — which every ping would have survived |

Step 11 proves the 32 MHz oscillator and its GPIO 19 gate are *alive*: a design
with no clock does not answer a ping at all, so a correct design ID has already
cleared both. Step 15 is what proves the clock's *rate*, judged against the MCU's
own timebase — a ratio, so it cannot say which side is wrong on its own; step 3
is the one that ties the MCU clocks down. What only step 15 can see is the
counter falling short: an oscillator that stalled for part of the window and
recovered answers every ping and still fails here, which is what makes it worth
having in the soak.

## What it reports but does not judge

**OTP `FLASH_DEVINFO`.** Measured on hardware, this part answers `0x9` for chip
select 0, which is the correct 2 MiB, and `0x0` for chip select 1 even though a
2 MiB device is fitted and working there. So one of the two is right and the
other is not, and nothing in the numbers themselves says which. That is the
reason this step prints them and stops: nothing in the firmware sizes a memory
from here. Flash comes from what the image was linked for, and the DRAM from the
SDK's own detection.

**The PSRAM identity.** The fitted device sweeps clean across its whole range
but reports `KGD 0x0B, EID 0x43` rather than AP Memory's `0x5D`, so it is not the
`APS1604M-3SQR-SN` the schematic calls for. Identity and function are separate
questions; the test answers the second and no test can answer the first. Reading
the package marking would settle it. `memid` prints the raw Read-ID bytes at
three clock rates for offline comparison; see
[the diagnostics reference](diagnostics-reference.md#the-memid-command).

Step 7 re-reads those bytes on every run, in the one window the datasheet
allows: a global reset, the 50 ns settling time, then a serial Read-ID under the
33 MHz ceiling, with QPI re-entered immediately after in the same pass. The
boot-time capture cannot be trusted for this — it is only legal on a cold start,
and after a warm reboot the device is still in QPI from the previous session, so
the serial Read-ID the SDK issues returns nonsense. The per-run read is what
makes the reported bytes meaningful whichever way the board arrived at the menu.

## What it does not check, and why

**Supply voltage.** On a Pico this is ADC3 via GPIO 29. Where GPIO 26–29 go on the
Forgix is documented nowhere in this repository, and RP2350-E9 makes driving an
unknown net worse than leaving it alone. The die temperature sensor is on-die and
touches no pad, which is why it is the one analog reading taken.

**The GPIO 0 lockup.** The built-in test runs long after `BSP_Init()` installed
the pull-up that stands in for the missing 10K resistor. It cannot test the window
between power-up and the first instruction, because nothing runs there. See
[the README's lockup section](../README.md#firmware-lockup-cause-and-fix);
fitting the resistor is the actual fix.

**Anything destructive to the MCU.** No SRAM march test, no deliberate watchdog
reset, no second-core launch. Every MCU check is read-only, so a run cannot leave
the board in a state a power cycle is needed to escape. The PSRAM is the one
deliberate exception: step 7 overwrites the whole device and global-resets it,
which is safe because nothing in the firmware stores data there, and the device
is re-initialised inside the same pass that reset it — an abort at any point
leaves nothing that the next run or a power cycle is needed to repair.

## The other menu entries

- **`2` Built-in test soak** — the whole sequence on repeat with a tally of runs
  and runs-with-a-failure, until a key stops it. This is the burn-in: a fault that
  appears once an hour will not show up in a single 20-second pass.
- **`3` One test at a time** — re-run a single step without sitting through the
  fourteen that already passed.
- **`4` Board report** — the same facts, printed without verdicts, for when the
  question is what the board *is* rather than whether it is well.
- **`5` Blinker** — red, green, blue at 1 Hz, forever. A blink that keeps time
  still says the loop is alive long after a one-shot test has stopped saying
  anything.
- **`6` Advanced blinker** — heartbeat, colour wheel and aurora. These ran from
  the host in `scripts/test_hardware.sh`; in firmware they need nothing but
  power.
- **`c`** drops to the `forgix>` shell, and the shell's `menu` command comes back.
- **`r`** reboots; **`b`** enters BOOTSEL for reflashing without unplugging.

Any key aborts a running test or show and returns to the menu. Anything that
changed the LED puts it back first, including on an abort.

## When something hangs

Every part of this has its own watchdog progress marker, so a reset attributes
itself: `MENU` (7), `IBIT` (8), `EFFECT` (9). Read them off a frozen board with
`scripts/decode_scratch.py`; the procedure is in
[the diagnostics reference](diagnostics-reference.md#reading-a-frozen-board).
