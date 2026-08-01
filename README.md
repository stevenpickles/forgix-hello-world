# Forgix Hello World

Standalone RP2354 firmware that configures the Forgix Trion T8 FPGA and then
provides a USB serial shell for its RGB LED and button peripherals.

The supported build environment is Windows with Git Bash. Tool paths default to:

```bash
export EFINITY_HOME="/c/Efinix/Efinity/2026.1"
export PICO_SDK_PATH="/c/RPi/pico-sdk-2.3.0"
export GHDL_BIN_PATH="/c/Forgix/GHDL/ghdl-mcode-6.0.0-ucrt64/bin"
```

Run `./scripts/bootstrap.sh` first. Once all dependencies are available:

```bash
./scripts/test.sh
./scripts/build_all.sh
./scripts/flash.sh
```

Pico SDK 2.3.0 must include its TinyUSB submodule. If the SDK came from a
source archive without submodules, set `PICO_TINYUSB_PATH` to a compatible
TinyUSB checkout; the scripts also recognize `build/tinyusb`. UF2 generation
and flashing require picotool 2.3.0, matching the SDK.

See `docs/register-map.md` for the runtime protocol. The Efinity project files
build a placed-and-routed T8F49 passive-SPI image locally and verify the board
pinout plus setup/hold timing before firmware compilation begins.

GitHub Actions runs the open-source verification path on every push and pull
request: VHDL 2008 simulation with GHDL 6.0.0, deterministic image-embedding
and Efinity-helper checks, static project-metadata validation, and an RP2354 USB
firmware compile with a 2 MB flash-budget gate against Pico SDK 2.3.0. The
firmware CI build embeds `tests/fixtures/fpga-test.bin`; it is a compile fixture,
not a loadable FPGA image. Licensed Efinity synthesis and hardware tests remain
local.
