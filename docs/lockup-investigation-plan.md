# Firmware lockup: instrumented diagnosis + mitigations (two failure modes)

> **Living document.** This is the working plan for the lockup investigation on
> branch `feature/6/investigate-firmware-lockup`. The executing agent should keep
> it current: check off completed work, correct anything the code contradicts,
> and append evidence to the [Learnings log](#learnings-log) as runs complete.
> Soak results also go in the results table of `docs/usb-cdc-debugging.md`.

## Context

The Forgix (RP2354A + Trion T8) board exhibits **two distinct observed failure modes**:

1. **USB shell hang, ~9–10 min (~475 s):** with the CDC shell connected to the Windows 11 host, the foreground-driven LED **and** the 1 Hz serial heartbeat stop together while Windows still lists COM3. Clearly MCU-side (serial dies too) and activated by the USB data path.
2. **LED-only hang, ~45–75 min:** the USB-compiled-out 2 Hz LED image also freezes — on a USB-C wall charger, a PC port, *and* a USB battery pack. LED frozen (on or off); a power cycle recovers it. This **corrects** the documented variant-A ">45 min pass" in `docs/usb-cdc-debugging.md` — that run simply wasn't long enough. Because the LED is driven by the **FPGA**, "LED frozen" here is ambiguous: MCU hang *or* FPGA configuration/clock loss while the MCU runs on. Occurring on all power sources, it is board-local, not host-driven.

The firmware currently has no watchdog and no observability, so each hang yields almost no evidence. This change makes both failure modes **self-attributing**: instrumented images (watchdog fed only by the foreground loop; progress markers + health snapshots in watchdog scratch registers that survive reset; reset-cause reporting; runtime FPGA health checks with auto-reconfigure) plus a long-run PowerShell soak harness. This supersedes most of the serial A–H ladder in `docs/usb-cdc-debugging.md`.

### Findings that shape the design (verified)

- **Untimed flush loop:** Pico SDK 2.3.0 `puts()`/`vprintf()` call `stdio_flush()` → `stdio_usb_out_flush()` = `do { tud_task(); } while (tud_cdc_write_flush());` (`pico-sdk/src/rp2_common/pico_stdio_usb/stdio_usb.c:134-142`) — a genuine infinite-block site. The firmware prints an unsolicited status line every 1 s forever after boot until a key arrives, with **no** DTR/connected check (`firmware/src/application/application_console.c:161-169`). Leading suspect for mode 1.
- **Known-issue matches (mode 1):** pico-sdk **#1932** (USB serial locks up; LED + serial stop together; RP2350-preferential; open, milestone 2.4.0); alarm-pool wedge modes (#1120/#1552/#1500) that would permanently starve the background `tud_task()` (stdio_usb services TinyUSB from a low-priority IRQ + one-shot alarm retry); host bus-suspend wedging endpoint state (TinyUSB on this chip cannot distinguish suspend from disconnect). TinyUSB 0.18.0 lacks post-0.18 RP2350 dcd fixes (issue #3533); the RP2040-E15 UFRAME_FIX is compiled out on RP2350. RP2350-E12 clock constraint is satisfied by the default 150/48 MHz clocks.
- **Time math is clean:** `bsp_time_now_ms()` = `to_ms_since_boot(get_absolute_time())` (64-bit source, `firmware/src/bsp/bsp_time.c:5-7`); `deadline_reached()` uses wrap-safe signed subtraction (`application_console.c:35-37`). A naive `time_us_32()` wrap (2³² µs = 71.6 min — inside the mode-2 window) is **not** present in tracked code, but any restored stash code must be audited for it, and the wall-clock time of mode-2 hangs should be logged precisely to test the ~71.6 min coincidence.
- **MCU cannot hang in the LED path:** the runtime FPGA bus is fully bounded bit-banged GPIO (`firmware/src/bsp/bsp_fpga.c:85-124`, fixed 1 µs busy-waits, no ready-polling). So a frozen LED with no watchdog reset ⇒ FPGA-side failure, not MCU. FPGA hooks available: `CDONE` (pin 5), status pin (pin 6), `bsp_fpga_ping()` design ID, register readback; FPGA oscillator is enabled by GPIO 19 (`PIN_OSC_EN`).
- **Mode-2 hypothesis set:** (a) FPGA loses configuration or its oscillator/clock stops (thermal / power-rail droop — 45–75 min variability fits thermal drift); (b) MCU hang independent of USB (timer/alarm or silicon); (c) brownout/power event that wedges rather than cleanly resets. Reset-cause + watchdog + FPGA ping distinguish all three.
- Decisions confirmed with user: implement instrumented firmware + harness (not docs-only); include the DTR gate in the first USB image.

## Implementation

### 1. BSP module `bsp_watchdog` (into shared `forgix_bsp` lib)

New `firmware/src/bsp/bsp_watchdog.{h,c}`; aggregate the header in `firmware/src/bsp/bsp.h` (required by `scripts/check_firmware_layers.py`).

```c
enum { BSP_WATCHDOG_SNAPSHOT_SLOTS = 3 };
typedef enum { BSP_BOOT_POWER_ON, BSP_BOOT_BROWNOUT, BSP_BOOT_WATCHDOG, BSP_BOOT_OTHER } bsp_boot_reason_t;
void     bsp_watchdog_start(uint32_t timeout_ms);   // watchdog_enable(timeout_ms, true); use 5000 ms
void     bsp_watchdog_feed(void);
bsp_boot_reason_t bsp_watchdog_boot_reason(void);   // POWMAN chip-reset reason + watchdog_enable_caused_reboot()
void     bsp_watchdog_marker_set(uint32_t marker);  // watchdog_hw->scratch[0]
uint32_t bsp_watchdog_marker_get(void);
void     bsp_watchdog_snapshot_set(uint32_t slot, uint32_t value);  // scratch[1+slot]
uint32_t bsp_watchdog_snapshot_get(uint32_t slot);
```

Scratch[0..3] are free (SDK reserves scratch[4..7]). 5 s timeout is 10× any bounded stdio timeout (500 ms `PICO_STDIO_USB_STDOUT_TIMEOUT_US` + 1 s `PICO_STDIO_DEADLOCK_TIMEOUT_MS`). `BSP_BOOT_BROWNOUT` from the RP2350 POWMAN chip-reset register is the direct brownout detector for mode 2.

### 2. BSP module `bsp_usb` (separate lib + stub — `forgix_bsp` is shared with the USB-free image)

New `firmware/src/bsp/bsp_usb.{h,c}` and `firmware/src/bsp/bsp_usb_stub.c`; header aggregated in `bsp.h`.

```c
typedef struct {
    bool     connected;        // stdio_usb_connected() — DTR
    bool     suspended;        // tud_suspended()
    uint32_t write_available;  // tud_cdc_write_available()
    uint32_t activity_count;   // bumped in tud_cdc_tx_complete_cb + tud_cdc_rx_cb (unclaimed by pico_stdio_usb — verified)
    uint32_t frame_number;     // usb_hw->sof_rd (advances 1/ms while host sends SOF; frozen = suspend)
} bsp_usb_health_t;
bsp_usb_health_t bsp_usb_health(void);
bool bsp_usb_connected(void);
void bsp_usb_service(void);   // tud_task() iff FORGIX_FOREGROUND_USB_SERVICE=1, else no-op
```

Stub returns `connected=false`, zero counters, no-op service; links into the USB-free target.

### 3. BSP FPGA health additions (`bsp_fpga.h/.c`)

- `bool bsp_fpga_cdone(void);` (read `PIN_CDONE`) — config-loss indicator.
- `bool bsp_fpga_reconfigure(void);` — re-runs the existing static `configure()` + ping validation (refactor of `bsp_fpga_init` internals), so the diagnostics layer can attempt recovery after a runtime FPGA failure.

### 4. Application-layer diagnostics (keeps 100 % Ceedling gate, mock-driven)

New `firmware/src/application/application_diagnostics.{h,c}` — seed from `stash@{0}^3` on feature/5 (2 Hz LED scaffold, `led_only_main.c`, CMake second-target pattern). **Audit stash code for `time_us_32`-style wrap-unsafe timing when restoring.**

- Markers: `LOOP`, `CONSOLE_READ`, `CONSOLE_WRITE`, `COMMAND`, `USB_SNAPSHOT`, `FPGA_CHECK`.
- `application_diagnostics_start()`: report boot reason + retained scratch (`diag: watchdog-reset marker=… loop=… usb=… fpga=… flags=…`, or `diag: brownout-reset …`, or `diag: clean boot`) **before** enabling the watchdog. In the LED-only image the same report is emitted as an LED blink code (no console): red blinks = watchdog (count = marker value), yellow blinks = brownout, white = power-on. Then `bsp_watchdog_start(5000)`.
- `application_diagnostics_poll()` (first call in runner loop): feed watchdog → `marker_set(LOOP)`; 2 Hz LED toggle; once per second:
  - `marker_set(FPGA_CHECK)`: `bsp_fpga_ping()` + `bsp_fpga_cdone()` + readback of the LED register after write. On failure: record in scratch (fpga-fail counter + which check failed), attempt `bsp_fpga_reconfigure()`, and on success resume with a distinctive 3-flash white recovery signature. **A frozen LED that self-recovers this way proves the MCU was alive and the FPGA lost config — mode 2 attributed without any instruments.**
  - `marker_set(USB_SNAPSHOT)` (USB image only): snapshot `bsp_usb_health()` into scratch — slot 0 = loop-seconds counter, slot 1 = `activity_count`, slot 2 = packed `frame_number | connected<<16 | suspended<<17 | (write_available==0)<<18`. In the LED-only image slot 1 carries the fpga-fail/reconfig counter instead.
- LED health colors (USB image, live no-reset observability): **green** 2 Hz = connected + activity advancing; **red** = connected but activity frozen >5 s (endpoint/stack wedge); **blue** = DTR low; **magenta** = suspended / SOF frozen. Document that this overrides `color`/`off` visuals during the investigation (commands still reply `ok`, so `test_hardware.ps1` passes).
- `firmware/src/application/application_console.c`: set `CONSOLE_READ` before `bsp_console_getchar_timeout_us`, `CONSOLE_WRITE` before status/echo/prompt bursts, `COMMAND` before dispatch; **gate the unsolicited status block on `bsp_usb_connected()`** (approved DTR-gate mitigation).
- `firmware/src/application/application_runner.c`: `application_diagnostics_start()` + `application_diagnostics_poll()` before `application_console_poll()`.
- Add a `diag` shell command in `application.c` printing live counters + last-reset report; update help text, `firmware/tests/test_application.c`, and the help assertion at `scripts/test_hardware.ps1:174`.

### 5. USB-free control image — now fully instrumented

Restore `firmware/src/diagnostics/led_only_main.c` from the stash (dir exempt from layer check and coverage gate); link against `forgix_bsp_usb_stub`. It runs the same diagnostics poll (watchdog, markers, FPGA check + auto-reconfigure, LED boot-blink report) — this image is the primary mode-2 instrument.

### 6. CMake (`firmware/CMakeLists.txt`)

- `forgix_bsp`: add `bsp_watchdog.c`, link `hardware_watchdog` (+ POWMAN access via SDK structs).
- New `forgix_bsp_usb` (links `pico_stdlib pico_stdio_usb tinyusb_device`) and `forgix_bsp_usb_stub`.
- `forgix_application`: add `application_diagnostics.c`; `forgix_hello_world` links `forgix_bsp_usb`.
- New target `forgix_led_only_diagnostic` (stdio usb+uart off) linking the stub.
- `option(FORGIX_FOREGROUND_USB_SERVICE OFF)`: when ON, define `PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK=0` on the app target and `FORGIX_FOREGROUND_USB_SERVICE=1` on `forgix_bsp_usb`; runner then services TinyUSB via `bsp_usb_service()`. Never call `tud_task()` from the foreground while the background IRQ task is active.
- `scripts/flash.sh`: accept an optional image name (default `forgix_hello_world`). CI needs no change; optionally assert the new UF2 in `scripts/build_firmware.sh`.

### 7. Tests

- Handwritten mocks (pattern: `firmware/tests/support/mock_bsp_time.c`): `mock_bsp_watchdog.{h,c}`, `mock_bsp_usb.{h,c}`; extend the existing FPGA mock with `cdone`/`reconfigure`.
- New `firmware/tests/test_application_diagnostics.c`: boot-report branches (watchdog/brownout/clean), feed-per-poll, LED color branches, FPGA-check failure → reconfigure → recovery-signature path, snapshot packing, stall threshold.
- Extend `test_application_console.c` (marker ordering, both DTR-gate branches) and `test_application.c` (`diag` command, help text).
- Gates: `python scripts/check_firmware_layers.py` and `./scripts/test_ceedling.sh` stay green at 100 % line+branch.

### 8. Soak harness `scripts/soak_serial.ps1`

Params: `-Port COM3 -BaudRate 115200 -Dtr $true -Rts $false -DurationMinutes 0 -GapWarnSeconds 5 -GapFailSeconds 30 -SendIntervalSeconds 0 -PingCommand status -LogDirectory build/soak-logs`. Opens the port **once**, never toggles control lines, never auto-reopens. Timestamped log of every RX line, TX pings with sequence numbers, gap warnings. On failure: log last-RX/last-seq/max-gap, print the Stage 4 capture checklist (record LED color; Device Manager/USBView; ETW; one manual reopen; `picotool reboot -f -u` last), exit. A `diag:` boot report arriving on the still-open port after a watchdog reset is captured automatically.

### 9. Decision tree (observation → verdict → next step)

Mode 2 (LED-only image, any power source — run first, it needs no PC):

| Observation | Verdict | Next |
| --- | --- | --- |
| LED freezes then self-recovers with white recovery signature; reconfig counter grows | **FPGA lost configuration/clock; MCU fine.** | FPGA power-rail/oscillator/thermal track: scope 3V3/FPGA rail + OSC_EN clock at failure time, review Efinity design constraints; USB hang may be unrelated → keep mode-1 track open |
| LED freezes; watchdog reset (red blink code); marker=`FPGA_CHECK`/`LOOP` | MCU hang without USB — timer/alarm or silicon | Log precise wall-clock time-to-hang across runs; if tightly ~71.6 min, hunt a 32-bit µs wrap (SDK paths); else escalate to Raspberry Pi forum/pico-feedback with marker data |
| Yellow boot blink (brownout reset) | Supply droop | Power-integrity track: different cable/supply, measure rails |
| LED freezes; no watchdog reset; no self-recovery; ping still OK (readback mismatch counter set) | FPGA serving bus but LED logic stuck | Efinity design/clock-domain review |

Mode 1 (USB image + soak harness):

| Observation after hang | Verdict | Next |
| --- | --- | --- |
| WD reset, marker=`CONSOLE_WRITE`, activity frozen, SOF advancing, connected | Foreground stuck in stdio write path (untimed flush / #1932 family) | Re-run with `FORGIX_FOREGROUND_USB_SERVICE=ON`; cured ⇒ alarm-pool/IRQ wedge → adopt foreground servicing. Not cured ⇒ dcd-level → TinyUSB 0.20.0 via `PICO_TINYUSB_PATH` / `build/tinyusb` |
| WD reset, marker=`CONSOLE_WRITE`, SOF **frozen**, suspended | Host bus-suspend wedges endpoint | One run with selective suspend disabled; keep DTR gate + skip writes while suspended |
| WD reset, marker=`CONSOLE_READ` | Blocked in input/mutex path (nominally bounded — unexpected) | Foreground-service build; inspect stdio_usb mutex |
| WD reset, marker=`COMMAND` or `FPGA_CHECK` | FPGA/SPI-adjacent stall — correlate with mode-2 findings | Treat as mode-2 root cause surfacing earlier under USB load |
| WD reset, counters all healthy | Watchdog too tight / feed bug | Raise to 8 s, re-run |
| No reset, LED **red**, harness gap | Endpoint wedged, foreground alive (bounded 500 ms writes drop) | Manual reopen: recovers ⇒ host-session problem; not ⇒ TinyUSB upgrade experiment |
| No reset, LED **green**, harness gap | Device delivering; host not completing transfers | Stage 4 Windows ETW/USBView capture, then Stage 5 host variation |
| LED **magenta** | Suspend while running | As suspend row |
| COM port gone / clean-boot report after hang | Reset / power / re-enumeration event | Cross-check with mode-2 brownout evidence; cable/port variation |

If mode 2's root cause is FPGA/power, re-evaluate mode 1: a supply/FPGA event at 475 s could plausibly cascade into the USB stack; fix mode 2 first, then re-run the mode-1 soak.

### 10. Soak sequence (log each run + precise wall-clock times in the docs results table)

1. **Run 0 (mode 2, no PC needed):** instrumented LED-only image on the USB-C wall supply, ≥ 2 h (covers the 71.6 min window twice). Note exact freeze/recovery times.
2. **Run 1 (mode 1):** instrumented USB image, harness passive, DTR on — reproduce (expected <10 min; cap 20 min).
3. **Run 2:** USB image, no client, 20 min — does the DTR-low path alone wedge (status writes now gated — note in log).
4. **Run 3 (branch per decision tree, 20 min each):** (a) foreground-service build, (b) selective-suspend disabled, (c) TinyUSB 0.20.0, (d) flush-avoidance output path (`vsnprintf` + `putchar`, which skips `stdio_flush`).
5. Any survivor of 20 min → 2 × 30 min to clear (per docs rule). Mode-2 clears need ≥ 2 h.
6. Heisenbug contingency: if an instrumented image never hangs, remove changes one at a time (DTR gate first, then FPGA check, then snapshot cadence, then watchdog), falling back to the A–H ladder only for the surviving delta.

### 11. Mitigations to reduce lockup probability (prioritized, root-cause-independent)

1. **Watchdog + boot-reason/marker report in the production image** — 5 s recovery + evidence (confound note: converts hang→reset; keep out of pure boundary tests).
2. **Runtime FPGA health check + auto-reconfigure** — turns a permanent LED/peripheral freeze into a logged, self-healing blip if mode 2 is FPGA-side.
3. **DTR-gate unsolicited output** (implemented here).
4. **Bounded writes** — adopt the flush-avoidance output path in `bsp_console.c` permanently if the untimed flush is implicated.
5. **Foreground `tud_task` servicing** — removes the alarm-pool dependency; adopt if run 3(a) cures.
6. **Host policy** — keep-open client (soak harness); device-level selective-suspend disable as documented operator option.
7. **Upstream track** — watch pico-sdk #1932 (milestone 2.4.0) and TinyUSB post-0.18 dcd fixes; plan SDK/TinyUSB upgrade once experiments confirm relevance.

### 12. Docs

- `docs/usb-cdc-debugging.md`: **correct the results log** (variant A did not pass — LED-only image freezes at ~45–75 min on all power sources; power cycle recovers); retitle/extend scope to both failure modes; add "Stage 3 implementation" (marker table, scratch layout, LED colors + blink codes, decision trees, build knobs, `soak_serial.ps1` usage); note the instrumented images supersede serial variants B–G where the trees resolve them.
- `README.md`: update the stability-investigation paragraph (no longer "narrowed to USB" — two modes) and point to the soak harness and this plan.

## Execution order & verification

1. BSP modules (`bsp_watchdog`, `bsp_usb` + stub, `bsp_fpga` additions) → CMake → app diagnostics + console markers + DTR gate → restore + audit LED-only target from stash.
2. Mocks + tests green: `python scripts/check_firmware_layers.py`, `./scripts/test_ceedling.sh` (100 % gate).
3. `./scripts/build_firmware.sh` — both UF2s build; flash-budget gate still passes.
4. Write `scripts/soak_serial.ps1`; dry-check param parsing without hardware.
5. Docs updates; commit on `feature/6/investigate-firmware-lockup`.
6. Hardware (user-run): flash LED-only image first (Run 0 on wall supply), then USB image soaks per the sequence; interpret via the decision trees.

Critical files: `firmware/CMakeLists.txt`, `firmware/src/application/application_console.c`, `firmware/src/application/application_runner.c`, `firmware/src/application/application_diagnostics.c` (new, seed from stash@{0}^3), `firmware/src/bsp/bsp_watchdog.{h,c}` + `bsp_usb.{h,c}` + `bsp_usb_stub.c` (new), `firmware/src/bsp/bsp_fpga.{h,c}` (health additions), `firmware/src/diagnostics/led_only_main.c` (restore), `scripts/soak_serial.ps1` (new), `scripts/flash.sh`, `docs/usb-cdc-debugging.md`, `README.md`.

## Learnings log

Append dated entries as implementation and soak runs produce evidence. Keep verdicts
tied to the decision-tree rows above; when a hypothesis is confirmed or eliminated,
strike it through in the hypothesis list rather than deleting it.

| Date | Source (run / code finding) | Evidence | Verdict / plan change |
| --- | --- | --- | --- |
| 2026-08-01 | User observation | USB-disabled 2 Hz LED image freezes at ~45–75 min on wall charger, PC port, and battery pack; LED frozen; power cycle recovers | Mode 2 established; variant-A "pass" row in usb-cdc-debugging.md invalidated; plan restructured to two tracks |
