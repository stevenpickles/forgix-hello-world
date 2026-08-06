# Building USB-enabled picotool 2.3.0 on Windows

This project uses Raspberry Pi Pico SDK 2.3.0 and requires the matching
picotool 2.3.0 host utility. The tool must include libusb support to inspect,
load, verify, erase, and reboot the board's RP2354. The file-only picotool copy
that the Pico SDK may build while generating a UF2 is not sufficient for
hardware access.

The supported local environment is Windows with Git Bash. The procedure below
records the paths and compatibility choices verified on the Forgix development
host.

## Known working layout

| Component | Git Bash path |
| --- | --- |
| Pico SDK 2.3.0 | `/c/RPi/pico-sdk-2.3.0` |
| picotool 2.3.0 source | `/c/RPi/picotool-2.3.0` |
| picotool build tree | `/c/RPi/picotool-2.3.0-build-usb` |
| picotool installation | `/c/RPi/picotool-2.3.0-install-usb` |
| libusb 1.0.29 root | `/c/Forgix/libusb-1.0.29` |
| libusb header directory | `/c/Forgix/libusb-1.0.29/include` |
| libusb static library | `/c/Forgix/libusb-1.0.29/VS2019/MS64/static/libusb-1.0.lib` |

The build also uses Visual Studio 2022 Community's x64 C++ toolchain and CMake
3.28.1 from STM32CubeCLT 1.20.0 (`CMAKE_EXE`). `scripts/build_picotool.sh`
carries no built-in paths beyond VsDevCmd's fixed VS2022 Community location:
every other input is pinned in `scripts/env.local.sh` or exported per run.

## Why picotool must be built from source

The picotool 2.3.0 source release does not provide the required standalone
Windows executable in its release assets. Raspberry Pi publishes prebuilt SDK
tools separately, but an exact, USB-enabled 2.3.0 Windows build was not
available when this environment was established. Building the tagged source
also keeps picotool aligned with Pico SDK 2.3.0.

A picotool executable built without libusb still handles files and can convert
ELF output to UF2, but `picotool version` prints the following warning and the
hardware commands are absent:

```text
This version of picotool was compiled without USB support.
```

The Pico SDK-generated copy originally found under this repository's `build/`
tree had been configured with `PICOTOOL_NO_LIBUSB=1`. Do not reuse that build
directory for the USB-enabled host tool. picotool's own build documentation
also requires a fresh build tree when changing `PICOTOOL_NO_LIBUSB`.

References:

- [picotool 2.3.0 build instructions](https://github.com/raspberrypi/picotool/blob/2.3.0/BUILDING.md)
- [picotool 2.3.0 release](https://github.com/raspberrypi/picotool/releases/tag/2.3.0)
- [libusb 1.0.29 release](https://github.com/libusb/libusb/releases/tag/v1.0.29)

## Prepare the source and libusb

Clone the exact picotool tag from Git Bash if the source directory is not
already present:

```bash
git clone --depth 1 --branch 2.3.0 \
  https://github.com/raspberrypi/picotool.git \
  /c/RPi/picotool-2.3.0
```

Download `libusb-1.0.29.7z` from the official libusb release and extract it to:

```text
C:\Forgix\libusb-1.0.29
```

Confirm the two files used by the build:

```bash
test -f /c/Forgix/libusb-1.0.29/include/libusb.h
test -f /c/Forgix/libusb-1.0.29/VS2019/MS64/static/libusb-1.0.lib
```

The extracted package also contains a `VS2022/MS64/static` library. That
archive was built with a newer VS2022 link-time-code-generation format than
the installed MSVC 19.35 toolset. Linking it failed with `LNK1257`/`C1900` and
an IL version mismatch. The VS2019 x64 static library is binary-compatible
with the installed VS2022 compiler and produced the verified build. Static
linking also avoids deploying `libusb-1.0.dll` beside picotool.

## Build and install

From the repository root in Git Bash, run:

```bash
./scripts/build_picotool.sh
```

The Bash entry point validates every input path and invokes
`scripts/build_picotool.cmd`. The Windows helper:

1. Loads the Visual Studio 2022 x64 developer environment.
2. Configures a fresh NMake build with `PICOTOOL_NO_LIBUSB=OFF`.
3. Passes the libusb header and static library paths explicitly.
4. Enables `PICOTOOL_FLAT_INSTALL` so the executable and CMake package share a
   predictable directory.
5. Builds and installs picotool.
6. Rejects the result unless it reports version 2.3.0 without the no-USB
   warning and exposes the `load` command.

The default installed executable is:

```text
C:\RPi\picotool-2.3.0-install-usb\picotool\picotool.exe
```

The script takes every machine-specific path from the environment or from
`scripts/env.local.sh` (see `scripts/env.local.example.sh`), which is the
durable place to pin them; exports in the shell work for a one-off run. The
build and install trees default to `<PICOTOOL_SOURCE>-build-usb` and
`<PICOTOOL_SOURCE>-install-usb`:

```bash
export PICOTOOL_SOURCE="/c/RPi/picotool-2.3.0"
export PICO_SDK_PATH="/c/RPi/pico-sdk-2.3.0"
export LIBUSB_ROOT="/c/Forgix/libusb-1.0.29"

./scripts/build_picotool.sh
```

The optional Pico SDK mbedTLS submodule was not present on the verified host,
so picotool reported that signing and hashing support was omitted. That does
not affect USB inspection, load, read-back verification, erase, or reboot.

## Expose the installation to Git Bash and CMake

Set all three variables in each Git Bash session, or add them to the local
shell profile:

```bash
export PICOTOOL_BIN_PATH="/c/RPi/picotool-2.3.0-install-usb/picotool"
export picotool_DIR="$PICOTOOL_BIN_PATH"
export PATH="$PICOTOOL_BIN_PATH:$PATH"
```

`PATH` lets the repository scripts execute `picotool`. `picotool_DIR` points
the Pico SDK's CMake package lookup at the installed tool. Adding only the
executable to `PATH` is not sufficient for all Pico SDK discovery paths.

Verify the environment:

```bash
hash -r
type -a picotool
picotool version
picotool help load
./scripts/bootstrap.sh
```

The expected version line is similar to:

```text
picotool v2.3.0 (Windows, MSVC-19.35.32216.1, Release)
```

There must be no `compiled without USB support` warning, and
`scripts/bootstrap.sh` must report the environment as ready.

## Entering BOOTSEL on Forgix

The normal software request is:

```bash
picotool reboot -f -u
```

With the Forgix Hello World firmware running, this command works without issue:
the USB serial device disappears and the board enumerates in BOOTSEL. Confirm
the transition before writing flash:

```bash
picotool info -a
```

The output should identify the device as an RP2350-family processor, report
`boot type: bootsel`, and show 2048K of flash. COM3 normally disappears while
the board is in BOOTSEL.

Software entry depends on the firmware currently running on the RP2354. During
the first flash, the tested board was still running the factory
`forge_fpga_loader` image. It exposed USB serial on COM3 and picotool printed:

```text
The device was asked to reboot into BOOTSEL mode.
```

The factory firmware did not leave the board in BOOTSEL, however. Repeated
probes still found the RP2350 USB serial interface and reported no accessible
BOOTSEL device. This was specific to the initial firmware, not a syntax error
in the picotool command; software BOOTSEL works normally after this project's
firmware has been installed.

Use the hardware fallback for the first flash or recovery:

1. Disconnect USB-C and any other source of board power.
2. Jumper the I/O-ring `PRG` (`PROGRAM`) pin directly to `GND`.
3. Reconnect USB-C while keeping the jumper fitted.
4. Wait for Windows to enumerate the RP2350-family boot device.
5. Remove the jumper after the boot device appears.

Do not use `SW1`; that button is connected to the FPGA. Do not connect `PRG`
to 3.3 V or 5 V. The Forgix getting-started guide describes the same fallback
as holding the program pin low while power is applied:

- [Getting Started With Forgix](https://www.hackster.io/adam-taylor/getting-started-with-forgix-4c72eb)

## First Forgix flash

Build the FPGA and firmware images before flashing:

```bash
./scripts/build_all.sh
```

With the board already in BOOTSEL through the `PRG`-to-`GND` procedure, load,
read back, and execute the UF2 without `-f`:

```bash
picotool load -v -x build/firmware/forgix_hello_world.uf2
```

Successful output reaches 100 percent for both phases and ends with:

```text
Verifying Flash:      [==============================]  100%

  OK

The device was rebooted to start the application.
```

After reboot, the USB serial interface should return as COM3. The FPGA's dim
blue LED is the expected register-reset state and indicates that the RP2354
loaded the embedded FPGA image. The firmware's serial shell can then exercise
design-ID readback, RGB output, and the FPGA button counter; see the
[runtime register map](register-map.md).

## Troubleshooting

### picotool still reports no USB support

Confirm that the script used the dedicated `-build-usb` directory and the
explicit libusb header and library paths. Do not reconfigure the Pico
SDK-generated no-libusb build in place.

### Final link fails with `LNK1257` or `C1900`

Use `VS2019/MS64/static/libusb-1.0.lib`, not the newer VS2022 static archive,
with the installed MSVC 19.35 compiler.

### `picotool reboot -f -u` returns to COM3

The normal way to enter BOOTSEL is `./scripts/bootsel.sh`: it wraps this same
`picotool reboot -f -u` call and then waits for the bootloader to enumerate, so
a successful run means the board is actually ready to flash rather than only
that the request was accepted. If the board returns to COM3 instead, its
running firmware did not complete the software BOOTSEL transition. Use the
powered-off `PRG`-to-`GND` recovery procedure, confirm with `picotool info -a`,
then flash without `-f`. See [the README](../README.md) for the cases the
script cannot rescue.

### COM3 cannot be opened after flashing

Close PuTTY or any other serial terminal because Windows serial ports are
exclusive. If no process owns the port, disconnect USB-C for several seconds
and reconnect it normally to reset the Windows USB serial device.
