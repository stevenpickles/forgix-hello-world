# Scripts

Every script that needs a tool path sources `scripts/env.sh` for it — source
the file yourself (never run it) when you need those paths in your own shell;
`--print` reports what it resolved. The supported host environment is Git Bash
on Windows, and the same scripts run unchanged inside the private
`forgix-build` container used in CI (see [the FPGA CI
document](../docs/fpga-ci.md)).

## Build and flash

| Script | What it does |
| --- | --- |
| [bootsel.sh](bootsel.sh) | Reboot the board into BOOTSEL mode with `picotool reboot -f -u`, then poll until the bootloader actually enumerates; `--check` reports state without changing it |
| [bootstrap.sh](bootstrap.sh) | Check that every required build tool and environment variable is present, printing ok/missing for each |
| [build_all.sh](build_all.sh) | Run the full build in one step: `build_fpga.sh` then `build_firmware.sh` |
| [build_firmware.sh](build_firmware.sh) | Configure and build the RP2354 firmware with the FPGA image embedded, producing the UF2s and enforcing the 2 MB flash budget |
| [build_fpga.sh](build_fpga.sh) | Synthesize, place, and route the T8F49 bitstream with Efinity, verify the reports, and convert the result to the embeddable binary image |
| [build_picotool.cmd](build_picotool.cmd) | Windows helper invoked by `build_picotool.sh`: configures, builds, installs, and verifies USB-enabled picotool 2.3.0 under the VS2022 developer environment |
| [build_picotool.sh](build_picotool.sh) | Build and install USB-enabled picotool 2.3.0 from source on Windows with the Visual Studio 2022 toolchain and libusb (drives `build_picotool.cmd`) |
| [efinity_hex_to_bin.py](efinity_hex_to_bin.py) | Convert an Efinity passive-SPI hexadecimal image to compact bytes |
| [embed_image.py](embed_image.py) | Convert a compact FPGA binary to deterministic C source and header files |
| [env.sh](env.sh) | Tool locations for the Forgix build scripts — `EFINITY_HOME`, `PICO_SDK_PATH`, `GHDL_BIN_PATH`, `PICOTOOL_BIN_PATH`, `PICO_TINYUSB_PATH`, `FORGIX_FIRMWARE_BUILD_DIR`. Source it; do not run it |
| [env.local.example.sh](env.local.example.sh) | Template for `scripts/env.local.sh`, the untracked file that pins one machine's tool locations; `env.sh` sources it when present |
| [flash.sh](flash.sh) | Flash a built UF2 to the board with picotool, verify USB support first, then confirm the image actually in flash matches what was requested |
| [run_efinity.cmd](run_efinity.cmd) | Windows counterpart to `run_efinity.sh`, invoked by `build_fpga.sh`: runs Efinity's `setup.bat` then one headless compile of the project |
| [run_efinity.sh](run_efinity.sh) | POSIX sibling of `run_efinity.cmd`, invoked by `build_fpga.sh`: one headless Efinity compile, run as a separate process so Efinity's environment rewrite cannot leak into the caller |

## Verification and gates

| Script | What it does |
| --- | --- |
| [check_efinity_reports.py](check_efinity_reports.py) | Verify the Efinity build used the Forgix pinout, met its clock target, and ran timing against the project SDC with every interface port constrained |
| [check_firmware_layers.py](check_firmware_layers.py) | Protect the dependency boundary between application code and board support |
| [check_repository.py](check_repository.py) | Validate repository-owned Efinity metadata without invoking the licensed tools |
| [render_coverage_summary.py](render_coverage_summary.py) | Render Cobertura coverage totals as a GitHub-compatible HTML summary fragment |
| [test.sh](test.sh) | Simulate the FPGA testbenches with GHDL 6.0.0 (VHDL-2008), asserting at error level |
| [test_ceedling.sh](test_ceedling.sh) | Run the Ceedling unit-test and coverage tasks, directly inside the forgix-build container or via Docker with the pinned image on a host |

## Formatting

| Script | What it does |
| --- | --- |
| [check_firmware_style.py](check_firmware_style.py) | Score each firmware layer against its profile in [the firmware style rubric](../docs/firmware-style-rubric.md); only the mechanically decidable rules are checked, and `--strict` is the CI gate |
| [format_firmware.sh](format_firmware.sh) | Apply the firmware formatting rules with clang-format (rules in `firmware/.clang-format`, rubric in [the firmware style rubric](../docs/firmware-style-rubric.md)); `--check` verifies without writing, which is what CI calls |
| [format_vhdl.sh](format_vhdl.sh) | Apply the VHDL formatting rules with VSG (rules in `fpga/vsg.yaml`, rubric in [the VHDL style rubric](../docs/vhdl-style-rubric.md)); `--check` verifies without writing, which is what CI calls |

## Diagnostics and hardware

| Script | What it does |
| --- | --- |
| [decode_scratch.py](decode_scratch.py) | Decode the diagnostics scratch registers read out of a frozen Forgix board over SWD, when the USB-free image has no console to ask (see [the diagnostics reference](../docs/diagnostics-reference.md)) |
| [soak_serial.ps1](soak_serial.ps1) | Long-duration USB CDC soak harness: holds the serial port open for the whole run and never reopens it after a failure, since a reopen would destroy the evidence |
| [test_hardware.ps1](test_hardware.ps1) | Implementation of the physical hardware smoke test, invoked by `test_hardware.sh` — do not call directly |
| [test_hardware.sh](test_hardware.sh) | Run the local hardware smoke test against a flashed board (requires Git Bash on Windows; wraps `test_hardware.ps1`) |

## Script self-tests

| Script | What it does |
| --- | --- |
| [test_efinity_tools.py](test_efinity_tools.py) | Unit tests for Efinity image conversion and build-report validation |
| [test_embed_image.py](test_embed_image.py) | Regression checks for deterministic FPGA image embedding |
| [test_render_coverage_summary.py](test_render_coverage_summary.py) | Unit tests for the GitHub coverage summary renderer |
