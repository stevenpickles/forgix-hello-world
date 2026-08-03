# Forgix Hello World

Standalone RP2354 firmware that configures the Forgix Trion T8 FPGA and then
provides a USB serial shell for its RGB LED and button peripherals.

Firmware is split into three layers under `firmware/src`: `main.c` only
initializes the board and dispatches the application, `application/` owns shell
and command behavior, and `bsp/` owns every Pico SDK and board-hardware detail.
Application code consumes the aggregate `bsp.h` interface instead of including
Pico SDK headers directly.

The supported build environment is Windows with Git Bash. Tool locations live in
`scripts/env.sh`, which every script sources for itself, so the scripts run in a
fresh shell with no setup:

```bash
EFINITY_HOME       /c/Efinix/Efinity/2026.1
PICO_SDK_PATH      /c/RPi/pico-sdk-2.3.0
GHDL_BIN_PATH      /c/Forgix/GHDL/ghdl-mcode-6.0.0-ucrt64/bin
PICOTOOL_BIN_PATH  /c/RPi/picotool-2.3.0-install-usb/picotool
PICO_TINYUSB_PATH  resolved from the SDK submodule, or build/tinyusb
```

Any variable already set in the environment wins, so a non-default installation
only needs that one export. Source the file yourself when invoking `cmake`,
`ninja`, or `picotool` by hand; `--print` reports what it resolved:

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

### USB serial console

Open the board's USB serial port at 115200 baud with terminal local echo
disabled. The firmware provides device-side echo and a `forgix> ` prompt, maps
both CR and LF to one command terminator, and supports Backspace/Delete,
`Ctrl-C` to cancel a line, `Ctrl-U` to erase it, and `Ctrl-L` to redraw it.

After boot, the board reports status once per second until it receives a key.
Status output is suppressed while a partial command is present. Ten seconds
after a completed command, idle status reporting resumes at ten-second
intervals. The following commands control the terminal policy:

```text
echo on|off             Enable or disable device-side character echo
watch <1..3600>|off     Report status at that interval, or disable idle reports
quiet                   Disable echo, prompts, and unsolicited status
interactive             Restore the default interactive behavior
```

An active `watch` stops as soon as a key is received so its output cannot
interrupt the next command. `scripts/test_hardware.sh` selects `quiet` mode
before parsing responses, keeping the physical smoke test deterministic.

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
by a pattern written across the start, middle and end of its range; `diag`
reports both memories. One open question remains: the device identifies itself
as `KGD 0x0B, EID 0x43` rather than AP Memory's `0x5D`, so the fitted part does
not match the `APS1604M-3SQR-SN` on the schematic even though it works correctly.
Reading the package marking would settle it.

See the [lockup investigation plan](docs/lockup-investigation-plan.md) for the
full record, including the wrong turns and why they were wrong, and the
[firmware lockup debugging plan](docs/usb-cdc-debugging.md) for the soak results
and the diagnostics still built into both images.

Run long sessions with `./scripts/soak_serial.ps1`, which holds the port open for
the whole run and never reopens it after a failure, since a single controlled
reopen is itself one of the experiments.

Application behavior can also be exercised without a board. The Ceedling toolchain
is pinned in a Docker image, so the same compiler, Unity, CMock, and coverage tools
run on Windows and in CI:

```bash
python scripts/check_firmware_layers.py
./scripts/test_ceedling.sh
```

The command runs the unit suite both normally and with coverage instrumentation.
Detailed HTML, Cobertura XML, and text coverage reports are written beneath
`firmware/build/ceedling/artifacts/gcov/gcovr/`. Pass explicit Ceedling tasks when a faster
development loop is useful, for example `./scripts/test_ceedling.sh test:all`.
Application policy is gated at 100% line and branch coverage. The
GitHub Actions summary renders color-coded line, function, and branch totals and
links to the detailed annotated HTML report in the downloadable test artifact.
CI artifact names include the workflow run ID and attempt number so downloaded
reports and firmware images can be traced back to an exact execution.

Pico SDK 2.3.0 must include its TinyUSB submodule. If the SDK came from a
source archive without submodules, set `PICO_TINYUSB_PATH` to a compatible
TinyUSB checkout; the scripts also recognize `build/tinyusb`. UF2 generation
and flashing require picotool 2.3.0, matching the SDK.

See `docs/register-map.md` for the runtime protocol. The Efinity project files
build a placed-and-routed T8F49 passive-SPI image locally and verify the board
pinout plus setup/hold timing before firmware compilation begins.

GitHub Actions runs the open-source verification path on every push and pull
request: Dockerized Ceedling application tests with BSP mocks and enforced
coverage, firmware-layering checks, VHDL 2008 simulation with GHDL 6.0.0,
deterministic image-embedding and Efinity-helper checks, static project-metadata
validation, and an RP2354 USB firmware compile with a 2 MB flash-budget gate
against Pico SDK 2.3.0. The application job publishes its JUnit, detailed HTML,
Cobertura XML, and text reports as a workflow artifact. The firmware CI build
embeds `tests/fixtures/fpga-test.bin`; it is a compile fixture, not a loadable FPGA
image. Licensed Efinity synthesis and hardware tests remain local.
