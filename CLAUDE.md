# CLAUDE.md

RP2350 (RP2354) firmware + Efinix T8F49 FPGA hello-world. Single-threaded
foreground loop; strict application/BSP layering (`scripts/check_firmware_layers.py`
enforces it — application code may include only `bsp.h`, every public `bsp_*.h`
must be aggregated there, no `hardware/`/`pico/` includes outside the BSP).

## Environment

`scripts/env.sh` must be **sourced, never executed**. Machine paths live in the
untracked `scripts/env.local.sh` (template: `env.local.example.sh`). Contract
variables: `EFINITY_HOME`, `PICO_SDK_PATH` (pico-sdk 2.3.0), `GHDL_BIN_PATH`,
`PICOTOOL_BIN_PATH`, `PICO_TINYUSB_PATH`, `FORGIX_FIRMWARE_BUILD_DIR`.

## Verification gates (run all before calling work done)

```bash
for s in scripts/*.sh; do bash -n "$s"; done
python -m compileall -q scripts
python scripts/check_repository.py
python scripts/test_efinity_tools.py
python scripts/test_embed_image.py
python scripts/test_decode_scratch.py
python scripts/check_protocol_constants.py && python scripts/test_check_protocol_constants.py
python scripts/check_firmware_layers.py
python scripts/check_firmware_style.py --strict
bash scripts/format_firmware.sh --check   # see caveat below
bash scripts/format_vhdl.sh --check       # VSG 3.35.0; -ap is mandatory in check mode
bash ./scripts/test.sh                    # GHDL benches, discovered from fpga/tb/tb_*.vhd
python scripts/test_render_coverage_summary.py
bash scripts/test_ceedling.sh             # Docker; gcovr gate 100% line + branch
```

- **clang-format is only authoritative inside the forgix-build container**
  (clang-format 18). Local LLVM disagrees. Run it via the pinned image:
  `docker run --rm --workdir /work -v <repo>:/work <image> bash scripts/format_firmware.sh --check`
  (image ref pinned in `scripts/test_ceedling.sh`).
- Ceedling measures `src/application/**` plus the two pure BSP modules
  (`bsp_fpga_health.c`, `bsp_memory_verdict.c`) at 100/100. The rest of the BSP
  is include-only by design — it needs hardware; do not mock the Pico SDK to
  inflate coverage.
- CI (`.github/workflows/ci.yml`) is the authoritative gate list; the tool-free
  prefix is the composite action `.github/actions/static-checks`.

## Builds

```bash
./scripts/build_fpga.sh        # Efinity → fpga/outflow/forgix_hello_world.bin
./scripts/build_firmware.sh    # both targets + UF2s + 2 MiB budget
```

Variants that must also compile (CI job `firmware-variants` covers them):
`-DFORGIX_QSPI_PSRAM=OFF` and `-DFORGIX_FPGA_AUTO_RECONFIGURE=ON`. Direct cmake
needs `-DPICO_SDK_PATH`, `-DPICO_TINYUSB_PATH`, `-DFPGA_IMAGE` (CI fixture:
`tests/fixtures/fpga-test.bin`) and typically `-DPICO_NO_PICOTOOL=1`.

## Protocol contract

`fpga/rtl/forgix_pkg.vhd` is the authority for every wire-protocol constant.
Counterparts live in `bsp_fpga.h`/`bsp_fpga.c`/`bsp_button.c`/`bsp_led.c`,
`docs/register-map.md`, `docs/ibit.md`, and `scripts/test_hardware.ps1`;
`scripts/check_protocol_constants.py` fails CI on any drift. **DESIGN_ID must
change whenever the register map changes meaning** — bump it everywhere the
checker checks, in one commit.

## Conventions

- PRs target `dev`. Commits: granular, why-focused prose, no co-author trailer.
- Firmware style: `docs/firmware-style-rubric.md` (banner sections, Allman,
  `( void )`, `/// <summary>` docs); checker profiles differ per layer.
- VHDL style: `docs/vhdl-style-rubric.md`; `--fix` rejects `-ap`, and
  `port_map_008` is not auto-fixable, so `--check` is the real gate.
- Python script tests are standalone (`python scripts/test_*.py` from repo
  root, no pytest); new scripts follow that pattern and get a CI step via the
  composite action.
- Hardware-only checks: `scripts/test_hardware.ps1` (serial, expects the
  current design ID), `scripts/soak_serial.ps1`, and anything touching the real
  PSRAM/USB — say explicitly when they have not been run.
