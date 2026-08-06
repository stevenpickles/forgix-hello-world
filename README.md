# Forgix Hello World

Standalone RP2354 firmware that configures the Forgix Trion T8 FPGA and then
provides a USB serial shell for its RGB LED and button peripherals.

Firmware is split into three layers under `firmware/src`: `main.c` only
initializes the board and dispatches the application, `application/` owns shell
and command behavior, and `bsp/` owns every Pico SDK and board-hardware detail.
Application code consumes the aggregate `bsp.h` interface instead of including
Pico SDK headers directly. See [the documentation index](docs/README.md) and
[the script index](scripts/README.md) for everything else in this repo.

There are two build environments, and the same scripts run unchanged in both.

The private `ghcr.io/stevenpickles/forgix-build` container is the canonical
verification environment. It bakes every toolchain — Efinity, GHDL, VSG,
clang-format, Ceedling, the Arm and host compilers — at pinned versions and
known `/opt` paths, it is what CI runs, and `./scripts/test_ceedling.sh` runs
it even when invoked from the host, digest-pinned so the toolchain under the
tests is byte-identical everywhere. Pulling it needs a one-time
`docker login ghcr.io` with a `read:packages` PAT, because the image also
carries the licensed Efinity tools; `forgix-verify` inside it checks the whole
tool contract (see `ci/forgix-build/Dockerfile` and
[the FPGA CI notes](docs/fpga-ci.md) for the one-time setup). The clang-format
gate is only authoritative there: the formatter version is pinned in the
image, and a locally installed LLVM can flag formatting the pinned version
accepts.

Windows with Git Bash is the supported host environment, and the only one for
anything that touches the board — flashing, BOOTSEL, the hardware smoke test,
and soak runs — because the container has no USB access. Native builds work
too. Tool locations live in `scripts/env.sh`, which every script sources for
itself, so the scripts run in a fresh shell with no setup:

```bash
EFINITY_HOME               /c/Efinix/Efinity/2026.1
PICO_SDK_PATH              /c/RPi/pico-sdk-2.3.0
GHDL_BIN_PATH              /c/Forgix/GHDL/ghdl-mcode-6.0.0-ucrt64/bin
PICOTOOL_BIN_PATH          /c/RPi/picotool-2.3.0-install-usb/picotool, or build/picotool-2.3.0/picotool
PICO_TINYUSB_PATH          resolved from the SDK submodule, or build/tinyusb
FORGIX_FIRMWARE_BUILD_DIR  build/firmware, or build/firmware-linux off Windows
```

Any variable already set in the environment wins, so a non-default installation
only needs that one export; the container pre-sets all of them and marks itself
with `FORGIX_BUILD_CONTAINER=1`. The firmware build tree is split per platform
because a CMake cache records absolute paths, so the Windows tree and the
container cannot share one directory — env.sh decides once, and the script that
builds into the tree and the script that flashes out of it cannot disagree
about where it is. Source the file yourself when invoking `cmake`, `ninja`, or
`picotool` by hand; `--print` reports what it resolved:

```bash
source ./scripts/env.sh --print
```

Build and install the USB-enabled picotool 2.3.0 host utility from Git Bash
with `./scripts/build_picotool.sh`. The script uses the Visual Studio 2022 x64
toolchain and the compatible VS2019 x64 static library from the libusb package
under `/c/Forgix/libusb-1.0.29`. It rejects the installation if picotool's USB
load command is unavailable.

USB support is what separates a picotool that can flash from one that cannot.
The Pico SDK fetches its own copy into `build/` while configuring the firmware;
that build has no libusb, so it converts UF2 files but has no `load` or `reboot`
command at all. `scripts/env.sh` therefore prefers the USB-enabled install, and
`bootstrap.sh` and `flash.sh` both check for `load` rather than merely finding
something named picotool. See
[Building USB-enabled picotool 2.3.0 on Windows](docs/picotool-windows.md) for
the complete source-build, environment, verification, BOOTSEL, and first-flash
procedure.

Run `./scripts/bootstrap.sh` first. Once all dependencies are available:

```bash
./scripts/test.sh
./scripts/build_all.sh
./scripts/flash.sh
```

With this project's firmware running, `./scripts/bootsel.sh` places the board in
BOOTSEL. It wraps `picotool reboot -f -u` and then waits for the bootloader to
enumerate, so success means the board is ready to flash rather than only that
the request was accepted. `./scripts/bootsel.sh --check` reports the current
state without changing it.

Two cases the script cannot rescue, both of which it names when it fails. The
original factory firmware did not complete the transition on the tested board;
and `forgix_led_only_diagnostic` has USB compiled out entirely, so no host tool
can ever reach it. For those, and as general recovery, power the board off,
jumper the I/O-ring `PRG` (`PROGRAM`) pin to `GND`, and reconnect USB with the
jumper fitted. Remove the jumper after the RP2350-family boot device appears,
then follow the verified load procedure in
[the picotool guide](docs/picotool-windows.md#first-forgix-flash).

With a flashed Forgix attached as `COM3`, close any serial terminal and run the
local hardware smoke test from Git Bash:

```bash
./scripts/test_hardware.sh
```

The test validates the USB command shell, FPGA design ID, status pin, and
register readback before presenting RGB heartbeat, color-wheel, and aurora LED
effects. Press `SW1` during the show to include the FPGA button counter in the
result. The default dim-blue LED state is restored at the end. Use
`./scripts/test_hardware.sh -Port COM4` to select another serial port or
`./scripts/test_hardware.sh -NoDazzle` for a quick functional check. This
physical test remains local because GitHub-hosted runners have no attached
Forgix hardware.

For an interactive check of the same hardware plus the microcontroller itself,
press `1` at the board's own menu instead. The on-board built-in test needs no
host tooling and covers ground the smoke test cannot reach from the far side of a
serial link — measured clock frequencies, the die temperature, the boot reason,
and both QSPI memories.

### USB serial console

Open the board's USB serial port at 115200 baud with terminal local echo
disabled. Whenever you get there, the board is already repeating:

```text
hello world - 47 - press any key
```

The count is seconds since boot rather than bytes sent, so it keeps advancing
while nothing is listening: reading `47` means the board has been up and
transmitting for the three quarters of a minute it took to find the port. The
board boots the instant it is powered, so anything printed once at boot is gone
before a terminal can be opened, and this is what replaces it. The message needs
nothing from the host, which is the point — it proves the assembly can transmit
and the host can receive even when host-to-board is broken.

Any key opens a single-keypress menu. That key is consumed by opening the menu
and is not also read as a selection, so reaching for "any key" cannot start
something.

```text
  1  Built-in test          the whole sequence, once
  2  Built-in test soak     repeat with a tally until a key is pressed
  3  One test at a time     re-run a single step without the other fourteen
  4  Board report           what this board is, without judging it
  5  Blinker                red, green, blue at 1 Hz until a key is pressed
  6  Advanced blinker       heartbeat, colour wheel, aurora
  c  Command shell          the forgix> prompt; `menu` returns here
  r  Reboot                 restart the board and reconfigure the FPGA
  b  Reboot to BOOTSEL      hand the board to the USB loader for reflashing
  ?  Redraw this menu
```

Any key aborts a running test or show and returns to the menu; anything that
changed the LED puts it back first. The menu names the FPGA as `ready` or
`UNAVAILABLE` in its header, because the tests that diagnose a dead FPGA are
reached from here. See [the built-in test reference](docs/ibit.md) for every
step, what a failure means, and what the sequence deliberately does not check.

`c` opens the command shell. It provides device-side echo and a `forgix> `
prompt, maps both CR and LF to one command terminator, and supports
Backspace/Delete, `Ctrl-C` to cancel a line, `Ctrl-U` to erase it, and `Ctrl-L`
to redraw it. Ten seconds after a completed command, idle status reporting starts
at ten-second intervals; it is suppressed while a partial command is present. The
full command surface is:

```text
hello                           Ping the FPGA and set the LED to confirm the link
color <r> <g> <b> [brightness]  Set the LED to that RGB and brightness (0..255 each)
off                             Turn the LED off
reset                           Reset and reconfigure the FPGA
```

```text
status                  One-line snapshot: FPGA id, status register, button, status pin
diag                    Full diagnostics report, both QSPI memories included
memid                   Re-read PSRAM and flash identity in the datasheet's legal window
menu                    Leave the shell and redraw the menu
help                    Print this command list
```

```text
echo on|off             Enable or disable device-side character echo
watch <1..3600>|off     Report status at that interval, or disable idle reports
quiet                   Disable echo, prompts, and unsolicited status
interactive             Restore the default interactive behavior
```

`status`, `diag`, `memid`, `menu`, and `help` all sit above the FPGA-ready gate
that `hello`, `color`, `off`, and `reset` sit behind: diagnosing a dead FPGA, and
getting back to the tests that do, must not be one of the things a dead FPGA
takes away. For what `diag` and `memid` actually report, see
[the diagnostics reference](docs/diagnostics-reference.md#the-diag-command) and
[the memid command](docs/diagnostics-reference.md#the-memid-command).

An active `watch` stops as soon as a key is received so its output cannot
interrupt the next command. `scripts/test_hardware.sh` sends `CR` then `c` to
reach the shell from whichever state it finds the board in — the port does not
reset the board, so a previous run may have left it anywhere — and then selects
`quiet` mode before parsing responses, keeping the physical smoke test
deterministic.

### Firmware lockup: cause and fix

The board had two long-duration failures that turned out to be one fault. The
USB shell stopped responding after nine to ten minutes, and the image with USB
compiled out froze after forty-five to ninety minutes on every power source. A
power cycle was the only recovery in both cases.

**Cause.** GPIO 0 is `XIP_CS1n`, the chip select for the secondary QSPI memory
that shares `SCLK` and `SD0..SD3` with the boot flash. RP2350 pads default to a
pull-down at power-up and the chip select is active low, so that device was
selected from reset and drove the shared data lines during flash reads.
Corrupted instruction fetches hung the core wherever it happened to be, which is
why no progress marker was ever recorded; the watchdog then reset into a bootrom
that could not read flash either, so nothing came back.

Raspberry Pi's *Hardware design with RP2350*, section 3.2, requires a 10K pull-up
on that net for exactly this reason. This board has no footprint for one.

**Fix.** `BSP_Init()` swaps the pad's power-up pull-down for a pull-up before
anything else runs. Measured against three unmitigated runs that failed at 240,
520 and 554 seconds, the same image then ran two hours clean, twice: once with
the secondary memory deselected and once with it live on the shared bus.

**This is a mitigation, not a cure.** Nothing runs before the bootrom's own flash
reads, so the window between power-up and the first instruction is still exposed
and occasional boot-time failures remain possible. Fitting a 10K pull-up from
GPIO 0 to 3V3 is the actual fix and is a board respin item.

The secondary memory is 2 MByte of QSPI PSRAM at `0x11000000`, enabled by
default through `FORGIX_QSPI_PSRAM`. It is sized by the SDK and verified at boot
by a pattern written across the start, middle and end of its range, through the
uncached alias so the answer describes the DRAM rather than the XIP cache; the
built-in test goes further and runs a moving-inversion sweep over the whole
device. `diag` reports both memories. The built-in test also re-reads the
identity every run in the datasheet's legal window -- global reset, then Read-ID
-- which is the only capture that stays meaningful after a warm reboot; the
boot-time bytes come from a device still in QPI mode and are nonsense then. One
open question remains about the device's identity — see
[the built-in test reference](docs/ibit.md#what-it-reports-but-does-not-judge).

The diagnostics built for the investigation remain in both images and are
documented in [the diagnostics reference](docs/diagnostics-reference.md); the
full investigation record, including the wrong turns and why they were wrong,
is preserved in git history.

Run long sessions with `./scripts/soak_serial.ps1`, which holds the port open for
the whole run and never reopens it after a failure, since a single controlled
reopen is itself one of the experiments. See
[the diagnostics reference](docs/diagnostics-reference.md#soak-harness) for how
its results are read.

Application behavior can also be exercised without a board. The Ceedling
toolchain (Ceedling 1.1.2, gcovr 8.6, host gcc) is pinned in the private
`forgix-build` image, and `./scripts/test_ceedling.sh` runs that image even
when invoked from the host, so the same compiler, Unity, CMock, and coverage
tools run on Windows and in CI. Contributors without image access can instead
install Ceedling 1.1.2 and gcovr 8.6 natively and run `ceedling` from
`firmware/`:

```bash
python scripts/check_firmware_layers.py
./scripts/test_ceedling.sh
```

The command runs the unit suite both normally and with coverage instrumentation.
Detailed HTML, Cobertura XML, and text coverage reports are written beneath
`firmware/build/ceedling/artifacts/gcov/gcovr/`. Pass explicit Ceedling tasks when a faster
development loop is useful, for example `./scripts/test_ceedling.sh test:all`.
Application policy is gated at 100% line and branch coverage. The
GitHub Actions summary renders color-coded line and branch totals (plus
functions when the report carries them; gcovr 8.x dropped that Cobertura
extension) and links to the detailed annotated HTML report in the downloadable
test artifact.
CI artifact names include the workflow run ID and attempt number so downloaded
reports and firmware images can be traced back to an exact execution.

Pico SDK 2.3.0 must include its TinyUSB submodule. If the SDK came from a
source archive without submodules, set `PICO_TINYUSB_PATH` to a compatible
TinyUSB checkout; the scripts also recognize `build/tinyusb`. UF2 generation
and flashing require picotool 2.3.0, matching the SDK.

See [the register map](docs/register-map.md) for the runtime protocol. The Efinity project files
build a placed-and-routed T8F49 passive-SPI image locally and verify the board
pinout plus setup/hold timing before firmware compilation begins.

GitHub Actions runs entirely inside the `forgix-build` image on every push and
same-repository pull request: a `verify` job (script and project-metadata
validation, firmware-layering checks, the clang-format and VSG format gates
([the firmware style rubric](docs/firmware-style-rubric.md) and
[the VHDL style rubric](docs/vhdl-style-rubric.md)),
VHDL 2008 simulation with GHDL 6.0.0, and the Ceedling application tests with
BSP mocks and enforced coverage), the Efinity synthesis job, and an RP2354 USB
firmware compile with a 2 MB flash-budget gate against Pico SDK 2.3.0 — the
last linking the bitstream that same run produced, falling back to the
`tests/fixtures/fpga-test.bin` compile fixture only if synthesis failed. The
verify job publishes its JUnit, detailed HTML, Cobertura XML, and text reports
as a workflow artifact. Hardware tests remain local.

Pushing a `v*` tag runs `release.yml`, which places and routes the T8F49
design, builds the RP2354 firmware against that exact bitstream using the
image's prebuilt picotool for UF2 generation, checks the image appears
byte-for-byte in the linked binary, and publishes the UF2, ELF, bitstream,
pinout and timing reports, and `SHA256SUMS` as a GitHub release. Fork pull
requests cannot pull the private image — they are never issued the registry
secret, however the workflow is rewritten — so they run the reduced
`fork-verify` job (the freely installable tools only), and the full pipeline
runs when a maintainer pushes the branch. See
[the FPGA CI notes](docs/fpga-ci.md) for the one-time image setup, the reason
the package is never granted to this repository, and the licensing constraints
that shape both.
