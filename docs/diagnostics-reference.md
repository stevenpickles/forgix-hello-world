# Diagnostics reference

What the firmware carries to make a hang or a hardware fault attributable
instead of silent: a watchdog with retained scratch registers, a runtime FPGA
health check, and the `diag` and `memid` shell commands.

## Scope

These are permanent features of both firmware images, not a temporary
investigation harness. The watchdog, its scratch registers, the FPGA health
check, and the `diag`/`memid` commands ship in `forgix_hello_world.uf2` and
`forgix_led_only_diagnostic.uf2` today and are not going away. The
investigation that produced them is closed; see
[the firmware lockup cause and fix](../README.md#firmware-lockup-cause-and-fix)
for the root cause and the mitigation. The full investigation record --
`docs/lockup-investigation-plan.md` and `docs/usb-cdc-debugging.md` as they
stood before this page replaced them -- is preserved in git history, not in
the working tree.

## Images and build knobs

| Image | Build | Purpose |
| --- | --- | --- |
| `forgix_hello_world.uf2` | default | Full shell with USB CDC and diagnostics |
| `forgix_led_only_diagnostic.uf2` | default | USB compiled out; LED and FPGA instrumentation only |

Both are produced by `./scripts/build_firmware.sh`. Load either with
`./scripts/flash.sh <image-name>`, defaulting to `forgix_hello_world`.

CMake options that shape the diagnostics (defaults as configured in
`firmware/CMakeLists.txt`):

| Option | Default | Effect |
| --- | --- | --- |
| `FORGIX_FOREGROUND_USB_SERVICE` | `OFF` | Moves TinyUSB servicing into the foreground loop via `BSP_UsbService()` and disables the SDK's background IRQ task. Never enable one without the other: two owners of `tud_task()` corrupt stack state |
| `FORGIX_FPGA_AUTO_RECONFIGURE` | `OFF` | Lets the runtime FPGA health check attempt a reconfiguration after a failure. Off by default because reloading the bitstream drives `CRESET_N` and rewrites 173 KB on every failing sample, which is itself a disturbance; turn it on to observe the recovery signature |
| `FORGIX_DIAGNOSTIC_UART` | `OFF` | Routes the USB-free image's diagnostics report over UART stdio instead of only the LED. Useful because the report survives the FPGA dying, and the LED cannot |
| `FORGIX_QSPI_PSRAM` | `ON` | Brings up the DRAM on QSPI chip select 1 (GPIO 0). Either way, the pad's power-up pull-down is swapped for a pull-up, which is the firmware's substitute for the 10K resistor this board has no footprint for |

## The diag command

`diag` takes no arguments and is gate-free: it is dispatched in
`application_process_command` (`firmware/src/application/application.c`)
above the FPGA-readiness gate, alongside `memid` and `menu`. The gate exists
to stop hardware commands from touching an FPGA that failed to configure;
`diag` and `memid` are exempt because a command that diagnoses a failure must
not be gated behind the hardware that failed.

Running `diag` prints three lines:

1. The memory report (`print_memory_report`): `Forgix: flash=<KiB> ok=<0|1>
   psram=<KiB> ok=<0|1> forced=<0|1> kgd=<hex> eid=<hex>`.
2. The boot report, replayed from what `application_diagnostics_start`
   captured at boot rather than re-read from hardware:
   `diag: boot=<reason> marker=<n> loop=<seconds> usb=<count>
   health=<8-hex-digit word>`. `boot=` is one of `power-on`, `brownout`,
   `watchdog`, or `other`.
3. A live counters line: `diag: uptime=<seconds>s connected=<0|1>
   suspended=<0|1> write=<n> activity=<n> sof=<n> fpga_fail=<n>
   fpga_reconfig=<n>`.

The boot report line is unchanged by time: it always describes the previous
boot, so it reads the same the first time and hours later. The live line is
current. Both are available at any time, whether or not the FPGA ever
configured, which is the whole point: the diagnostics that explain a broken
board must not depend on the board working.

## The memid command

`memid` reads out the raw identity bytes of both QSPI memories on the shared
bus, via `BSP_MemoryIdentityDump` (`firmware/src/bsp/bsp_memory.c`) and
`print_identity_dump` (`firmware/src/application/application.c`). It is
gate-free for the same reason as `diag`: the memories share nothing with the
FPGA, and the identity investigation is most needed exactly when the board is
being distrusted.

The output is one line per transaction, every one of the sixteen response
bytes shown (`BSP_MEMORY_IDENTITY_RESPONSE_BYTES = 16` -- eight bytes past the
documented manufacturer/KGD/EID fields, so the capture also shows whether a
device keeps driving, repeats its ID, or goes quiet):

- `cs0 flash 9F: ...` -- a plain `0x9F` Read-ID against the boot flash on
  chip select 0. This line is a sampling control: a known-good device on the
  same bus, read through the same direct-mode engine at the same clock and
  sample settings, so a correct flash ID is evidence the controller itself
  samples faithfully.
- `cs1 psram 9F @<rate>kHz: ...`, once per probe rate. The PSRAM is probed at
  three clock divisors -- 6, 30, and 150 against the 150 MHz system clock --
  giving 25 MHz, 5 MHz, and 1 MHz. An identity that is bit-identical across
  that 25x spread cannot be a marginal-sampling artefact.
- `cs1 psram: not probed; this image was built without PSRAM support` when
  the image has `FORGIX_QSPI_PSRAM` off; no chip-select-1 transaction is
  attempted.
- A closing `qpi re-entry: ok` or `error: qpi re-entry failed; psram is down
  until the next check` line, since every read tears the device out of QPI
  and the same call re-enters it before returning.

Two method facts worth keeping from the investigation that built this:

- **A reset ahead of Read-ID has to reach the device in whatever mode it is
  currently in.** The device can be left in QPI from a previous session or in
  plain SPI mode, and the two modes decode reset opcodes differently. The
  current code (`BSP_MemoryIdentityDump` and `BSP_MemoryPsramIdentify`) issues
  the quad-width reset pair first, unconditionally, then the serial reset
  pair, every time -- not only after a serial identification has already
  failed. The comment in the source explains why: "one of the two always
  applies, and the serial pair also cleans up after the quad opcodes a serial
  device would have decoded as noise." An earlier version of this code held
  the quad reset back until serial identification failed, out of concern that
  issuing it to a device already in SPI mode was itself a disturbance -- that
  concern is exactly why the two resets now run back-to-back inside one
  unbroken XIP-down window rather than being made conditional on which one
  should be needed.
- **An RX sampling-delay sweep across this bus was inconclusive, not
  negative.** `flash_do_cmd_cs` (the SDK helper) calls
  `connect_internal_flash`, which resets QMI `DIRECT_CSR` state and discards
  any divisor or delay written beforehand, so a delay set ahead of a call
  through that helper never reaches the transfer. That is why
  `_CsOperationSequence` in `bsp_memory.c` is a reimplementation of the
  direct-mode transfer rather than a wrapper around the SDK's helper: it is
  the only way to hold a clock divisor across the whole sequence of resets and
  reads that this file needs.

What the identity bytes mean and the open question of whether they name the
schematic's specified part are answered in
[the built-in test reference](ibit.md#what-it-reports-but-does-not-judge), not
here -- this command exists to produce the raw evidence, not to judge it.

## Watchdog scratch registers

`scratch[0..3]` of the watchdog hardware survive a watchdog reset; the SDK
reserves `scratch[4..7]` for its own reboot bookkeeping
(`firmware/src/bsp/bsp_watchdog.c`).

| Register | Contents |
| --- | --- |
| `scratch[0]` | Progress marker (table below) |
| `scratch[1]` | Loop-seconds counter (uptime in whole seconds) |
| `scratch[2]` | CDC activity counter (completed transfers; always 0 in the USB-free image) |
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

The two FPGA counters are narrow modulo fields here because they have to fit
in the packed word; their full-width values stay available live through
`diag`.

## Progress markers

Written to the watchdog marker register (`scratch[0]`) before entering each
path, so a watchdog reset names where the foreground stopped.

| Value | Marker | Set before |
| ---: | --- | --- |
| 1 | `LOOP` | Each foreground iteration, and after each completed sample |
| 2 | `CONSOLE_READ` | `BSP_ConsoleGetCharTimeoutUs` |
| 3 | `CONSOLE_WRITE` | Every console write, all of which reach the SDK's untimed stdio flush |
| 4 | `COMMAND` | Command dispatch |
| 5 | `USB_SNAPSHOT` | Reading USB health |
| 6 | `FPGA_CHECK` | CDONE, design-ID ping, and LED register readback |
| 7 | `MENU` | Drawing or dispatching the front-panel menu |
| 8 | `IBIT` | Running a built-in test step |
| 9 | `EFFECT` | Painting the blinker or the advanced blinker |

## Boot report and blink codes

Emitted before the watchdog is armed. In the USB image it is a serial line:

```text
diag: boot=watchdog marker=3 loop=612 usb=44 health=00010001
```

`boot=` is one of `power-on`, `brownout`, `watchdog`, or `other`; `brownout`
comes from the RP2350 POWMAN chip-reset register and is the direct test for a
supply droop. The remaining fields are the retained scratch registers from
before the reset. The `diag` shell command prints the same line plus live
counters at any time.

The USB-free image has no console, so it blinks the same report instead. The
code plays three times, because it cannot be replayed on demand:
power-cycling to watch it again resets the scratch registers it is reporting.

| Blink code | Meaning | Resting heartbeat |
| --- | --- | --- |
| White x1 | Clean power-on | Blue |
| Yellow x2 | Brownout reset | Yellow |
| Cyan x3 | Unclassified reset | Cyan |
| Red x N | Watchdog reset; N is the retained marker value, clamped to 1-8 | Red |

The resting color persists for the whole run, so the verdict stays readable
hours later even if the blink code was missed. Blue is nominal; any other
resting color means something happened. Only the blink count carries the
marker, so read it while it plays if the heartbeat comes up red.

Expect roughly 1.7 s of darkness after power-up while the FPGA is configured
and settles, then the blink code, then the heartbeat.

## Live LED health colors

The 2 Hz heartbeat carries current health without needing a reset. These
override the visual effect of `color` and `off`; the commands still answer
`ok`, so `test_hardware.ps1` passes.

| Color | Meaning |
| --- | --- |
| Green | Connected and CDC transfers are completing |
| Red | Connected, but data is queued and the transmit FIFO is not draining: an endpoint or stack wedge |
| Magenta | Bus suspended, or start-of-frame counter frozen for 5 s |
| Blue | Host has not asserted DTR |
| White x3 | FPGA was reconfigured and recovered |

**Correction to the historical record.** An earlier build keyed red on a bare
5-second gap in CDC activity, and the old investigation notes described red
as "no transfer completed for 5 s." That was an instrumentation defect: the
firmware's own idle-status line goes out every 10 s, so a plain activity gap
tripped the threshold on the firmware's own reporting cadence every cycle,
regardless of whether anything was actually wrong. The code now requires two
things together before it shows red: the transmit FIFO must be full (data is
queued) **and** not draining for 30 s
(`APPLICATION_DIAGNOSTICS_ACTIVITY_STALL_MS`,
`firmware/src/application/application_diagnostics.h`). That is what an
endpoint wedge actually looks like; a quiet link on its own is not a fault.
The frame-stall threshold behind magenta is unaffected and stays at 5 s
(`APPLICATION_DIAGNOSTICS_FRAME_STALL_MS`).

In the USB-free image there is no USB health to show, so the resting color
reports the last boot reason instead, per the table in the previous section.

The white recovery signature is the decisive observation for a stalled FPGA:
the LED is FPGA-driven, so a freeze that ends with several white flashes and a
growing reconfiguration count proves the MCU stayed alive and the FPGA lost
its configuration -- a distinction that otherwise needs instruments on the
board.

## Reading a frozen board

The USB-free image has no console, so when the LED stops there is no way to
ask it anything over serial. The scratch registers above survive a watchdog
reset and can be read out over SWD from a board that is still frozen, which is
the only route to the evidence in that state.

Read them with the board still powered, for example:

```text
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
    -c "init; halt; mdw 0x400d8000 8; mdw 0x4010002c 1; shutdown"
```

then decode them:

```text
python scripts/decode_scratch.py 0x6 0x1c2 0x0 0x48000000
```

`scripts/decode_scratch.py` takes the four scratch words positionally, plus
optional `--reason`, `--ctrl`, and `--chip-reset` values for the raw watchdog
and POWMAN registers, and prints the marker name, uptime, health-word fields,
and a plain-language reading against the same two tables above.

## Soak harness

`scripts/soak_serial.ps1` is the long-duration serial harness used to drive
the USB image for hours at a time and catch a hang with evidence attached.

```powershell
./scripts/soak_serial.ps1 -Port COM3 -DurationMinutes 20
./scripts/soak_serial.ps1 -Port COM3 -DurationMinutes 120 -SendIntervalSeconds 10
```

Full parameter list, as declared in the script's `param()` block:

| Parameter | Default | Purpose |
| --- | --- | --- |
| `-Port` | `COM3` | The Windows serial port to open |
| `-BaudRate` | `115200` | Serial baud rate |
| `-Dtr` | `$true` | DTR line state for the session |
| `-Rts` | `$false` | RTS line state for the session |
| `-DurationMinutes` | `0` | Run length; `0` runs until failure or Ctrl-C |
| `-GapWarnSeconds` | `5` | Idle time before a warning is logged |
| `-GapFailSeconds` | `30` | Idle time before the run is declared failed |
| `-SendIntervalSeconds` | `0` | Ping cadence; `0` disables pings and leaves the session passive |
| `-PingCommand` | `status` | The command line sent as a ping |
| `-LogDirectory` | `build/soak-logs` | Where the timestamped log file is written |
| `-ValidateOnly` | (switch) | Check parameters and the log directory, then exit without opening the port |

It opens the port exactly once and never reopens it, even after a failure.
That is deliberate: the single clean reopen an operator performs by hand
after a failure is itself the experiment that separates a wedged device from
a merely wedged host session, and a harness that reopened automatically would
destroy that distinction before anyone could observe it.

`-ValidateOnly` checks parameters and the log directory and exits without
touching a port, which is how the harness is exercised on a machine with no
board attached -- malformed port names, inverted gap thresholds, negative
durations, and an empty ping command are all rejected the same way they would
be with a board connected.

This one script stays PowerShell rather than joining the project's other
bash tooling: on the tested machine, `/dev/ttyS2` and `/dev/com3` are real
character devices that open successfully and honor `clocal`, but carry no
data, while .NET's `SerialPort` on the same port works normally. A bash
rewrite would need `pyserial`, which is not installed.
