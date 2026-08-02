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

**Status: implemented** on `feature/6/investigate-firmware-lockup`. Layer check,
Ceedling gate (100 % line and branch), and both firmware images build. The
sections below record the design as built; where the delivered code departs from
the original specification the difference is called out inline and in the
[Learnings log](#learnings-log). Hardware runs are outstanding — start with
[Run 0](#10-soak-sequence-log-each-run--precise-wall-clock-times-in-the-docs-results-table).

### 1. BSP module `bsp_watchdog` (into shared `forgix_bsp` lib) — done

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

### 2. BSP module `bsp_usb` (separate lib + stub — `forgix_bsp` is shared with the USB-free image) — done

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
bool bsp_usb_present(void);   // ADDED: true in the real lib, false in the stub
```

Stub returns `connected=false`, zero counters, no-op service; links into the USB-free target.

`bsp_usb_present()` was **added** beyond the original API. The diagnostics layer
needs a compile-time-decided but runtime-readable answer to "does this image have
a console at all", to choose between the serial boot report and the LED blink
code, without the application layer learning about build variants or including
SDK headers. Testing it is a matter of flipping one mock flag.

**Build trap (cost an hour, worth recording):** `forgix_bsp_usb` must link
`pico_stdio_usb` only, **never** `tinyusb_device`. The marked `tinyusb_device`
library defines `LIB_TINYUSB_DEVICE`, and the SDK's own
`pico_stdio_usb/include/tusb_config.h` is guarded by
`#if !defined(LIB_TINYUSB_HOST) && !defined(LIB_TINYUSB_DEVICE)` — it stands down
on the assumption that an application defining that symbol supplies its own
TinyUSB configuration. The result is `CFG_TUD_ENABLED=0` across the whole image:
`usbd.c` and `cdc_device.c` compile to nothing and the link fails on
`tud_suspended` / `tud_cdc_write_available`. Also note `PRIVATE` is wrong for a
static library here: these SDK libraries add their sources to the *consuming*
target, so they must remain in the public link interface to reach the executable.

### 3. BSP FPGA health additions (`bsp_fpga.h/.c`) — done

- `bool bsp_fpga_cdone(void);` (read `PIN_CDONE`) — config-loss indicator.
- `bool bsp_fpga_reconfigure(void);` — recovery entry point. **Implemented as a call to `bsp_fpga_init()`** returning `result.ready`, rather than as a separate refactor of the static `configure()`: `bsp_fpga_init` already is exactly "configure, settle, ping-validate", and it additionally refreshes the `fpga_ready` flag that the command layer gates on. Splitting it would have duplicated that. Worst-case duration is roughly 2.5 s (bitstream write + 500 ms CDONE timeout + 1500 ms settle), which fits inside the 5 s watchdog with margin.

`bsp_time_sleep_ms()` was **added** to `bsp_time`, for the boot blink code only.
It is documented as usable exclusively in bounded boot-time sequences that run
before the watchdog is armed; the foreground loop must never block.

### 4. Application-layer diagnostics (keeps 100 % Ceedling gate, mock-driven) — done

New `firmware/src/application/application_diagnostics.{h,c}` — seed from `stash@{0}^3` on feature/5 (2 Hz LED scaffold, `led_only_main.c`, CMake second-target pattern). **Audit stash code for `time_us_32`-style wrap-unsafe timing when restoring.**

- Markers: `LOOP`, `CONSOLE_READ`, `CONSOLE_WRITE`, `COMMAND`, `USB_SNAPSHOT`, `FPGA_CHECK` (values 1–6; the numeric value is what a red blink code counts out).
- `application_diagnostics_start()`: report boot reason + retained scratch **before** enabling the watchdog, then `bsp_watchdog_start(5000)`. The report is emitted as **one uniform line** rather than a different sentence per reason, so the soak harness can match it with a single pattern and the fields line up across runs:

  ```text
  diag: boot=watchdog marker=3 loop=612 usb=44 health=00010001
  ```

  `boot=` is one of `power-on`, `brownout`, `watchdog`, `other`. In the LED-only image the same report is a blink code: white ×1 power-on, yellow ×2 brownout, cyan ×3 other, red ×N watchdog where N is the retained marker clamped to 1–8.
- `application_diagnostics_poll()` (first call in runner loop): feed watchdog → `marker_set(LOOP)`; then, in this order, the one-second sample, the 2 Hz LED toggle, **a single LED write**, and the FPGA readback. Ordering the sample first means the heartbeat color always reflects health just read, and it makes the one LED write the same write the FPGA check reads back:
  - `marker_set(USB_SNAPSHOT)`: snapshot `bsp_usb_health()`; a stall is measured from the last time `activity_count` or `frame_number` actually changed.
  - `marker_set(FPGA_CHECK)`: `bsp_fpga_cdone()` + `bsp_fpga_ping()` + readback of the LED registers just written. On failure: increment the fpga-fail counter, attempt `bsp_fpga_reconfigure()`, and on success set the recovery signature and rewrite the LED, since a fresh configuration comes up with its registers cleared. **A frozen LED that self-recovers this way proves the MCU was alive and the FPGA lost config — mode 2 attributed without any instruments.**
  - Writing immediately before reading back matters: it makes the check measure the FPGA bus rather than whatever a `color` command left behind between polls. Without it, an operator typing `color` during a soak would forge an FPGA fault and trigger a spurious reconfiguration.

**Scratch layout, revised.** The original plan had slot 1 mean different things in
the two images (`activity_count` in the USB image, FPGA counters in the LED-only
image). As built, the layout is identical in both images, which keeps one code
path and one decoding table — and, more importantly, retains the FPGA evidence in
the USB image too, where a mode-2 fault surfacing under USB load is one of the
outcomes the decision tree looks for:

| Register | Contents |
| --- | --- |
| `scratch[0]` | progress marker |
| `scratch[1]` (slot 0) | loop-seconds counter |
| `scratch[2]` (slot 1) | `activity_count` (0 in the LED-only image via the stub) |
| `scratch[3]` (slot 2) | `frame_number` \| `connected`<<16 \| `suspended`<<17 \| `(write_available==0)`<<18 \| fpga-fail<<19 (7 bits) \| fpga-reconfig<<26 (6 bits) |

The two FPGA counters are narrow modulo fields in the packed word; their
full-width values stay available live through `diag`.
- LED health colors (USB image, live no-reset observability): **green** 2 Hz = connected + activity advancing; **red** = connected but activity frozen >5 s (endpoint/stack wedge); **blue** = DTR low; **magenta** = suspended / SOF frozen. Document that this overrides `color`/`off` visuals during the investigation (commands still reply `ok`, so `test_hardware.ps1` passes).
- `firmware/src/application/application_console.c`: set `CONSOLE_READ` before `bsp_console_getchar_timeout_us`, `CONSOLE_WRITE` before status/echo/prompt bursts, `COMMAND` before dispatch; **gate the unsolicited status block on `bsp_usb_connected()`** (approved DTR-gate mitigation).
- `firmware/src/application/application_runner.c`: `application_diagnostics_start()` + `application_diagnostics_poll()` before `application_console_poll()`.
- Add a `diag` shell command in `application.c` printing live counters + last-reset report; update help text, `firmware/tests/test_application.c`, and the help assertion at `scripts/test_hardware.ps1:174`.

### 5. USB-free control image — now fully instrumented — done

Restore `firmware/src/diagnostics/led_only_main.c` from the stash (dir exempt from layer check and coverage gate); link against `forgix_bsp_usb_stub`. It runs the same diagnostics poll (watchdog, markers, FPGA check + auto-reconfigure, LED boot-blink report) — this image is the primary mode-2 instrument.

**Stash audit result:** the restored `stash@{0}^3` code was clean. It already used
`bsp_time_now_ms()` with a wrap-safe signed comparison (`(int32_t)(now - deadline) >= 0`);
no `time_us_32`-style 32-bit microsecond arithmetic was present anywhere in it.
The 71.6-minute wrap hypothesis therefore has no candidate site in tracked or
restored project code, and can only be tested against SDK paths by logging
precise wall-clock times to failure.

### 6. CMake (`firmware/CMakeLists.txt`) — done

- `forgix_bsp`: add `bsp_watchdog.c`, link `hardware_watchdog` (+ POWMAN access via SDK structs).
- New `forgix_bsp_usb` (links `pico_stdlib pico_stdio_usb tinyusb_device`) and `forgix_bsp_usb_stub`.
- `forgix_application`: add `application_diagnostics.c`; `forgix_hello_world` links `forgix_bsp_usb`.
- New target `forgix_led_only_diagnostic` (stdio usb+uart off) linking the stub.
- `option(FORGIX_FOREGROUND_USB_SERVICE OFF)`: when ON, define `PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK=0` on the app target and `FORGIX_FOREGROUND_USB_SERVICE=1` on `forgix_bsp_usb`; runner then services TinyUSB via `bsp_usb_service()`. Never call `tud_task()` from the foreground while the background IRQ task is active.
- `scripts/flash.sh`: accept an optional image name (default `forgix_hello_world`). CI needs no change; optionally assert the new UF2 in `scripts/build_firmware.sh`.

### 7. Tests — done

- Handwritten mocks (pattern: `firmware/tests/support/mock_bsp_time.c`): `mock_bsp_watchdog.{h,c}`, `mock_bsp_usb.{h,c}`; `mock_bsp_time` also records `bsp_time_sleep_ms` so the blink code is assertable without slowing the suite. The FPGA mock needed no work: it is CMock-generated from the header, so `cdone`/`reconfigure` appeared automatically.
- New `firmware/tests/test_application_diagnostics.c`: boot-report branches (all four reasons, both output forms), blink-count clamping, feed-per-poll, every LED color branch, FPGA-check failure → reconfigure → recovery-signature path (including a sample with no heartbeat toggle), each of the five LED-register readback mismatches, failed reconfiguration, snapshot packing, stall thresholds.
- Extended `test_application_console.c` (marker coverage, both DTR-gate branches) and `test_application.c` (`diag` command, `diag extra` rejection, help text).
- Gates green: `python scripts/check_firmware_layers.py` reports 7 BSP headers; `./scripts/test_ceedling.sh` passes 60/60 with line-rate 1.0 and branch-rate 1.0 on all of `src/application/`.
- Firmware: both images build; `forgix_hello_world.bin` is 209 584 bytes against the 2 MB CI gate. `-DFORGIX_FOREGROUND_USB_SERVICE=ON` was verified to build and to compile `bsp_usb_service` into a `tud_task_ext` tail call with `PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK=0` applied.

### 8. Soak harness `scripts/soak_serial.ps1` — done

Params: `-Port COM3 -BaudRate 115200 -Dtr $true -Rts $false -DurationMinutes 0 -GapWarnSeconds 5 -GapFailSeconds 30 -SendIntervalSeconds 0 -PingCommand status -LogDirectory build/soak-logs`, plus `-ValidateOnly`. Opens the port **once**, never toggles control lines, never auto-reopens. Timestamped log of every RX line, TX pings with sequence numbers, gap warnings. On failure: log last-RX/last-seq/max-gap, print the Stage 4 capture checklist (record LED color; Device Manager/USBView; ETW; one manual reopen; `picotool reboot -f -u` last), exit non-zero. A `diag:` boot report arriving on the still-open port after a watchdog reset is matched explicitly and flagged as a reset rather than logged as ordinary output.

`-ValidateOnly` checks parameters and the log directory and exits without opening
the port; it was used to verify the harness with no board attached (valid
configurations accepted, and malformed port names, inverted gap thresholds,
negative durations, and an empty ping command all rejected).

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

### 12. Docs — done

- `docs/usb-cdc-debugging.md`: **correct the results log** (variant A did not pass — LED-only image freezes at ~45–75 min on all power sources; power cycle recovers); retitle/extend scope to both failure modes; add "Stage 3 implementation" (marker table, scratch layout, LED colors + blink codes, decision trees, build knobs, `soak_serial.ps1` usage); note the instrumented images supersede serial variants B–G where the trees resolve them.
- `README.md`: update the stability-investigation paragraph (no longer "narrowed to USB" — two modes) and point to the soak harness and this plan.

## Execution order & verification

1. ~~BSP modules (`bsp_watchdog`, `bsp_usb` + stub, `bsp_fpga` additions) → CMake → app diagnostics + console markers + DTR gate → restore + audit LED-only target from stash.~~ Done.
2. ~~Mocks + tests green: `python scripts/check_firmware_layers.py`, `./scripts/test_ceedling.sh` (100 % gate).~~ Done — 60/60 tests, line and branch rate 1.0.
3. ~~`./scripts/build_firmware.sh` — both UF2s build; flash-budget gate still passes.~~ Done — 209 584 bytes of a 2 097 152 byte budget.
4. ~~Write the soak harness; dry-check parsing without hardware.~~ Done as `scripts/soak_serial.ps1` with `-ValidateOnly`. A bash rewrite was attempted and reverted: MSYS serial devices open but carry no data on the tested machine.
5. ~~Docs updates; commit on `feature/6/investigate-firmware-lockup`.~~ Done.
6. **Next — hardware (user-run):** flash the LED-only image first (Run 0 on the wall supply, ≥ 2 h), then the USB image soaks per the sequence; interpret via the decision trees.

Critical files: `firmware/CMakeLists.txt`, `firmware/src/application/application_console.c`, `firmware/src/application/application_runner.c`, `firmware/src/application/application_diagnostics.c` (new, seed from stash@{0}^3), `firmware/src/bsp/bsp_watchdog.{h,c}` + `bsp_usb.{h,c}` + `bsp_usb_stub.c` (new), `firmware/src/bsp/bsp_fpga.{h,c}` (health additions), `firmware/src/diagnostics/led_only_main.c` (restore), `scripts/soak_serial.ps1` (new), `scripts/flash.sh`, `docs/usb-cdc-debugging.md`, `README.md`.

## Learnings log

Append dated entries as implementation and soak runs produce evidence. Keep verdicts
tied to the decision-tree rows above; when a hypothesis is confirmed or eliminated,
strike it through in the hypothesis list rather than deleting it.

| Date | Source (run / code finding) | Evidence | Verdict / plan change |
| --- | --- | --- | --- |
| 2026-08-01 | User observation | USB-disabled 2 Hz LED image freezes at ~45–75 min on wall charger, PC port, and battery pack; LED frozen; power cycle recovers | Mode 2 established; variant-A "pass" row in usb-cdc-debugging.md invalidated; plan restructured to two tracks |
| 2026-08-01 | Code finding — stash audit (`stash@{0}^3`) | Restored `application_diagnostics.c` and `led_only_main.c` use `bsp_time_now_ms()` with wrap-safe signed comparison; no `time_us_32` or other 32-bit µs arithmetic anywhere in the stashed code | The 71.6 min wrap hypothesis has **no candidate site in project code**. It survives only as an SDK-internal possibility, testable solely by logging precise wall-clock time-to-freeze across Run 0 repetitions |
| 2026-08-01 | Build finding — CMake/TinyUSB | Linking the marked `tinyusb_device` library alongside `pico_stdio_usb` silently disables CDC: `LIB_TINYUSB_DEVICE` makes the SDK's `pico_stdio_usb/include/tusb_config.h` stand down, so `CFG_TUD_ENABLED=0` and `usbd.c`/`cdc_device.c` compile to nothing | `forgix_bsp_usb` links `pico_stdio_usb` only. Recorded because the failure mode is a link error far from its cause, and because a *partial* version of this mistake could plausibly produce a working-but-degraded USB build — worth ruling out if TinyUSB behavior looks anomalous later |
| 2026-08-01 | Design change during implementation | Original scratch layout gave slot 1 different meanings per image | Unified: slot 1 is always `activity_count`, FPGA counters packed into slot 2 bits 19–31. One decode table, and FPGA evidence is now retained in the **USB** image too — which the mode-1 tree's "marker=`FPGA_CHECK`" row depends on |
| 2026-08-01 | Design change during implementation | FPGA readback originally compared against state written up to 500 ms earlier | The check now writes the commanded LED state and reads it back in the same step. Without this, an operator typing `color` mid-soak would forge an FPGA fault and trigger a spurious reconfiguration — corrupting exactly the mode-2 evidence the check exists to gather |
| 2026-08-01 | Verification | Layer check 7/7 BSP headers; Ceedling 60/60 with line-rate 1.0 and branch-rate 1.0 on `src/application/`; both UF2s build at 209 584 B against the 2 MB gate; `FORGIX_FOREGROUND_USB_SERVICE=ON` builds and emits `bsp_usb_service` → `tud_task_ext` | Implementation complete and gated. Hardware runs outstanding; Run 0 is next |
| 2026-08-02 | First hardware boot of `forgix_led_only_diagnostic` | Operator saw only the blue 2 Hz heartbeat and no boot blink code. Disassembly confirmed the stub linked correctly (`bsp_usb_present` → 0) and the blink path reached, so the firmware was behaving as written | **The instrument was unreadable, not broken.** The report was a single 150 ms flash ~2.4 s after power-up, bracketed by darkness — not reliably catchable, and unrepeatable because a power cycle clears the scratch it reports. Blink lengthened to 350 ms and repeated 3×; more importantly the **resting heartbeat now carries the boot reason** (blue nominal, red watchdog, yellow brownout, cyan other), which had been a wasted channel since the USB-free image had no USB health to display. A one-shot signal is the wrong design for evidence an operator must read hours later |
| 2026-08-02 | Run 1, instrumented USB image + soak harness | Two failures. Both ended with the board **off the USB bus**, recovered only by power cycle. First: ~4 min, then enumerated as RP2350 Boot + Mass Storage and `picotool save` read flash back as all `0x00` (erased flash reads `0xFF`). Second: **8 min 40 s**, vanished with no BOOTSEL and no watchdog re-enumeration. Telemetry 35 s before the second failure was entirely healthy: `connected=1 suspended=0 write=64 activity=391 sof=1865 fpga_fail=0 fpga_reconfig=0` | **Leading hypothesis moves to the XIP/QSPI flash path, away from the FPGA.** The MCU executes from XIP, so a flash read that stops being served hangs the core wherever it is -- no marker, no graceful path. The watchdog then resets, but the **bootrom** cannot read flash either, which is why nothing comes back and why the LED-only image stayed dark with no red blink code. Power cycle always recovers. It explains failure with and without USB, on every power source, since it is internal to the MCU. The FPGA check has never once reported a fault across every run |
| 2026-08-02 | Instrument verification | CDC activity counter confirmed incrementing under load: 187 -> 278 -> 374 across ~8 s. SOF confirmed advancing and wrapping correctly at 11 bits (`1354 + 4000 ms = 5354`, `5354 mod 2048 = 1258`, matching the observed value) | An earlier `activity=0` reading was misdiagnosed as a broken counter; that sample also had `connected=0`, so no CDC traffic had occurred yet and zero was correct. Both health fields are sound and the red "activity frozen" LED state is trustworthy |
