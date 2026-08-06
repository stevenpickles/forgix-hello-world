# Documentation

Reference material that is too detailed for the project hub to carry inline.
Start at [the root README](../README.md); come here for the depth behind it.

| Document | What it covers |
| --- | --- |
| [diagnostics-reference.md](diagnostics-reference.md) | Live reference for the diagnostics built into both firmware images: `diag`/`memid` commands, watchdog scratch layout, progress markers, boot blink codes, LED health colors, and the soak harness |
| [ibit.md](ibit.md) | The built-in test: all 15 steps, what each verdict means, what it deliberately does not check, and the canonical home of the PSRAM identity open question |
| [register-map.md](register-map.md) | The runtime SPI protocol between the MCU and the FPGA |
| [fpga-ci.md](fpga-ci.md) | The private Efinity build container image, and how the bitstream is nonetheless built in CI despite the license |
| [picotool-windows.md](picotool-windows.md) | Building USB-enabled picotool from source on Windows, and the BOOTSEL and first-flash procedure |
| [firmware-style-rubric.md](firmware-style-rubric.md) | The C style rubric enforced by `scripts/check_firmware_style.py` |
| [vhdl-style-rubric.md](vhdl-style-rubric.md) | The VHDL style rubric behind `fpga/vsg.yaml` |
| [datasheets/README.md](datasheets/README.md) | Annotated index of component datasheets (currently the APS1604M PSRAM) |

See also [the script index](../scripts/README.md).

The historical investigation documents — `FORGIX_PLAN.md`,
`lockup-investigation-plan.md`, and `usb-cdc-debugging.md` — were removed in
the documentation overhaul. The record is preserved in git history, and the
still-useful reference material moved into
[diagnostics-reference.md](diagnostics-reference.md).
