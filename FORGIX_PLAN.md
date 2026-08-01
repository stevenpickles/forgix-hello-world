# Forgix MCU-to-FPGA Hello World

## Summary

Build a standalone UF2 in which the RP2354 configures the Efinix Trion T8 at startup, then exposes a USB serial shell that commands FPGA-owned hardware.

```text
USB terminal
    ↕ USB CDC
RP2354 firmware
    ├─ boots/configures FPGA from embedded image
    └─ reads/writes FPGA registers over three-wire SPI
                         ↓
Trion T8 FPGA ── PWM → onboard RGB LED
             └─ debounce/count ← onboard button
```

This follows the board architecture: USB terminates at the RP2354, while the FPGA configuration connection is reused as a runtime SPI control path. The LED and button are connected to the FPGA. See the [Forgix specifications](https://forgix.tech/) and [board walkthrough](https://www.hackster.io/adam-taylor/getting-started-with-forgix-4c72eb).

## Implementation Changes

- Scaffold `fpga/`, `firmware/`, `scripts/`, and `tests/` areas.
- Implement the FPGA design in VHDL 2008:
  - 32 MHz clock input and synchronous reset.
  - Three-wire, MSB-first SPI slave on CS=`G3`, SCK=`F3`, SDIO=`F2`.
  - RGB PWM outputs on `E1/F1/G1`, accounting for the common-anode active-low LED.
  - Two-flop synchronization, approximately 10 ms debounce, and an 8-bit saturating button-press counter for SW1 on `G6`.
  - Keep version one limited to the onboard LED and button.
- Expose a byte-register protocol compatible with the established Forgix examples:
  - Commands: ping `0x9F`, reset `0x7F`, write `0x02 addr data`, read `0x03 addr`.
  - Registers: design ID/status, RGB values, global brightness, LED enable, button state, and button count.
  - Use design ID `0xB5`; initialize the LED to dim blue after configuration.
  - Preserve CS across a transaction, use idle-low runtime SCK, sample on rising edges, and provide SDIO turnaround before readback. The existing protocol and pin assignments are documented in the [public Forgix example](https://github.com/ATaylorCEngFIET/Forgix_hackster).
- Implement RP2354 firmware with the Pico SDK:
  - Embed the compact Efinity-generated FPGA binary into the UF2 through a CMake-generated C source file.
  - At boot, enable `GP19`, configure the FPGA over SPI0 at 8 MHz/mode 3 using `GP1–GP3`, sequence `CRESET_N` on `GP4`, send trailing clocks, and require `CDONE` on `GP5`.
  - Reconfigure `GP1–GP3` as a bit-banged three-wire runtime link after configuration; retain `GP6` for FPGA status.
  - Ping the FPGA before starting the application. Report configuration or ping failures over USB.
  - Expose a line-oriented USB CDC shell:
    - `hello` — command a cyan FPGA-generated LED output, read it back, and print `Hello from RP2354 -> FPGA B5`.
    - `color <r> <g> <b> [brightness]`
    - `off`
    - `status`
    - `reset`
    - `help`
  - Reject malformed or out-of-range arguments without partially updating FPGA registers.

## Git Bash Tooling and Workflow

- Make Git Bash on Windows the primary documented environment.
- Provide `scripts/bootstrap.sh` to:
  - Validate `EFINITY_HOME`, `PICO_SDK_PATH`, Python, CMake, Ninja, the ARM toolchain, GHDL, and picotool.
  - Accept Windows or POSIX-style environment paths and normalize them with `cygpath`.
  - Print exact installation guidance for missing dependencies without modifying the user's machine.
- Provide `scripts/build_fpga.sh`, `scripts/build_firmware.sh`, and `scripts/build_all.sh`.
  - `build_fpga.sh` invokes Efinity's Windows CLI from Git Bash, runs synthesis through bitstream generation, checks reports, and converts the passive-SPI hex image to binary.
  - `build_firmware.sh` configures CMake with Git Bash paths translated where Windows executables require native paths, generates the embedded image source in the build directory, and invokes Ninja.
  - `build_all.sh` runs both stages and prints the resulting UF2 path.
- Provide `scripts/flash.sh` using picotool, with a documented drag-and-drop UF2 fallback for BOOTSEL mode.
- Quote every path and test Windows executable return codes so installations under paths such as `Program Files` work correctly.
- Keep PowerShell commands out of the primary workflow; mention them only as an optional troubleshooting fallback.
- Use:
  - **Efinity** for required Trion T8 synthesis, placement, timing, and bitstream generation.
  - **VHDL 2008** for the FPGA logic.
  - **GHDL** for fast hardware-independent simulation.
  - **Pico SDK C/C++ with CMake/Ninja** for RP2354 firmware and USB CDC.
  - **picotool/UF2** for firmware installation and recovery.
  - **Python** only for deterministic FPGA image conversion and embedding.

The documented happy path will be:

```bash
export EFINITY_HOME="/c/Efinix/Efinity/2026.1"
export PICO_SDK_PATH="/c/RPi/pico-sdk-2.3.0"
export GHDL_BIN_PATH="/c/Forgix/GHDL/ghdl-mcode-6.0.0-ucrt64/bin"

./scripts/bootstrap.sh
./scripts/test.sh
./scripts/build_all.sh
./scripts/flash.sh
```

## Test Plan

- Simulate SPI ping, register reads/writes, reset defaults, SDIO turnaround, invalid commands, PWM duty cycles, button debounce, and press counting with GHDL.
- Run all automated tests through `./scripts/test.sh`.
- Build the FPGA with passing pin placement and timing at 32 MHz.
- Build the complete UF2 from a clean Git Bash checkout, including a workspace path containing spaces.
- Verify the embedded image is nonempty and the final firmware fits within the RP2354's 2 MB flash.
- Perform hardware acceptance testing:
  - One UF2 flash and USB power cycle configure the FPGA without a host loader.
  - USB reports successful `CDONE`, status, and FPGA ping.
  - `hello` changes the FPGA-driven LED and prints the expected round-trip message.
  - `color`, `off`, and `reset` produce the expected LED behavior.
  - Pressing SW1 changes the FPGA-maintained state and count returned by `status`.
  - Bad commands do not hang the shell or corrupt registers.

## Assumptions

- Target the current Forgix revision using RP2354 GPIOs `1–6` and `19` and the documented T8F49 pin map.
- Windows Git Bash is the primary and fully tested development shell.
- Efinity remains a native Windows installation launched from Git Bash.
- The FPGA image is embedded for standalone boot, so FPGA changes rebuild the UF2.
- USB CDC is the only user interface; no external components are required.
