# Forgix sequence diagrams

These PlantUML sources describe the current firmware and FPGA design. They are
kept as separate views so the overview diagrams remain readable while the
detailed diagrams preserve the ordering and failure branches needed for debug.

| Diagram | Purpose |
| --- | --- |
| [`power-on-simplified.puml`](power-on-simplified.puml) | Power-on through the steady-state foreground loop |
| [`power-on-detailed.puml`](power-on-detailed.puml) | Boot ROM exposure, FPGA configuration, PSRAM POST, watchdog recovery, USB startup and runtime health checks |
| [`ibit-simplified.puml`](ibit-simplified.puml) | User-visible initiated built-in test flow |
| [`ibit-detailed.puml`](ibit-detailed.puml) | All 15 IBIT steps, prerequisite skips, multi-pass waits and cleanup |
| [`mcu-fpga-interaction.puml`](mcu-fpga-interaction.puml) | Configuration-time and runtime responsibilities of the two ICs |
| [`mcu-fpga-data-flow.puml`](mcu-fpga-data-flow.puml) | Configuration transfer, register writes, register reads and dedicated return signals |

Render one diagram with a local PlantUML installation:

```text
plantuml -tsvg diagrams/power-on-simplified.puml
```

Or render every diagram from the repository root:

```text
plantuml -tsvg diagrams/*.puml
```

The PSRAM `CS#` warning in the boot diagrams is intentional. With the missing
external 10 kOhm pull-up, the RP2354 pad's power-on pull-down can select the
PSRAM during the boot ROM's flash traffic. Firmware changes the pad pull as its
first action, but it cannot protect execution that occurs before its first
instruction. A fitted pull-up to the PSRAM I/O rail keeps the device deselected
through that interval.
