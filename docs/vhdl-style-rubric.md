# VHDL formatting rubric

Applies to the FPGA tree, in two profiles:

| Profile | Files |
|---|---|
| **rtl** | `fpga/rtl/forgix_pkg.vhd`, `forgix_button.vhd`, `forgix_rgb_pwm.vhd`, `forgix_spi.vhd`, `forgix_hello_world.vhd` |
| **tb** | `fpga/tb/tb_button.vhd`, `tb_pwm.vhd`, `tb_spi_regs.vhd` |

Eight files, VHDL-2008, simulated with GHDL 6.0.0 (`bash scripts/test.sh`) and synthesized
with Efinity. Sections A to D are the rules, stated as the **rtl** profile has them;
section E lists where **tb** differs. A rule section E does not mention applies to both.

This is the VHDL counterpart to [the firmware style rubric](firmware-style-rubric.md), and it inherits that document's
governing principle: **a rule a machine cannot reproduce is not a rule.** The formatter half
is delegated wholesale to VSG 3.35.0, configured in `fpga/vsg.yaml`. The documentation half
(section D) cannot be mechanized and is marked human-judged throughout — that is a
concession, not an escape hatch, and section G says exactly which rules it covers.

The rules are derived from VSG's default ruleset, which is itself a distillation of the
published VHDL style guides, minus the defaults that would change what the hardware does and
minus one that fights the existing baseline. The baseline — 2-space indent, lowercase
keywords, `std_ulogic` throughout — was already consistent across all eight files and is kept.
This rubric standardizes and extends what is there; it does not import an alien style.

**Status: applied.** All eight files are at **zero violations** under `fpga/vsg.yaml`, and
all three testbenches pass — stopping at 2830 ns, 2577 ns and 45140 ns, the same simulated
times they reached before any of this, which is the evidence that none of it changed the
design. It went in as the three commits the Adoption section describes: the 588 mechanical
fixes, then the 65 hand repairs, then section D.

Two things worth knowing for the next pass. `--fix` rejects `-ap` outright, so the flag
belongs on the check run that verifies the fix and not on the fix itself. And adding a
comment inside a declarative block splits its alignment group, so section D's edits made
section C8's rules fail again — re-running `--fix` after writing comments is part of the
job, not a sign something went wrong.

> **Historical tense note.** The sections below are written in the present tense of the tree
> as it was *before* this was applied: the violation counts, "seven of the nine processes are
> unlabelled", "the eight files contain zero comments", and the Adoption ordering all describe
> the starting state. They are kept as written because they are the argument for each rule and
> the record of what the work cost. The rules themselves are current; only the observations
> about the code are historical.

Every rule has a stable ID. Renumbering them breaks the cross-references in `fpga/vsg.yaml`,
which cites them by ID in its comments.

---

## A. File and unit organization

### A1 — Context clause order

`library` clauses first, then `use` clauses, IEEE before `std`, and `work` last:

```vhdl
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.env.all;
use work.forgix_pkg.all;
```

All eight files already do this. `numeric_std` is present only where arithmetic or `unsigned`
is used — `tb_button.vhd` legitimately omits it, and that is not a violation. An unused
context clause is worse than a missing one: it implies a dependency the file does not have.

### A2 — One design unit pair per file

One entity and its architecture, or one package. `forgix_pkg.vhd` is the package; each other
file is exactly one entity/architecture pair. Nothing in this tree needs a second
architecture, and adding one would interact with A3 and with the binding rule in F1.

### A3 — Filename matches the unit

`forgix_spi.vhd` holds `forgix_spi`. `tb_button.vhd` holds `tb_button`. This is what lets
`scripts/test.sh` list analysis order by filename and lets Efinity's project XML find sources
by path.

### A4 — Analysis order is dependency order

`scripts/test.sh` analyses `forgix_pkg` first, then the leaf entities, then
`forgix_hello_world`. A new RTL file is inserted at its correct place in that list rather
than appended, because GHDL has no dependency solver here and the list is the dependency
graph written out by hand.

---

## B. Naming

**No rule in this section demands a rename.** Every one of them describes what the eight
files already do, and their purpose is to keep the next file consistent with them. This is
deliberate and it is not merely conservatism: the top-level entity name and every top-level
port name are bound externally, by `fpga/constraints/forgix_hello_world_io.isf` and
`fpga/forgix_hello_world.xml`. A rename there is a build break in a file no VHDL tool reads.

### B1 — Objects are snake_case

Signals, variables, ports, entities, architectures, procedures, packages: `rx_shift`,
`press_strobe`, `send_read_request_byte`, `forgix_rgb_pwm`.

### B2 — Active-low signals end `_n`

`button_n`, `cs_n`, `led_r_n`, `spi_sdio_out` vs `led_b_n`. The suffix is the only thing
standing between a reader and an inverted polarity bug, and it is applied without exception
in the current sources — including on the LED outputs, where `'0'` means lit.

### B3 — Types and subtypes end `_t`

`byte_t`, `state_t`.

### B4 — State enumeration literals end `_s`

`command_s`, `address_s`, `data_s`, `read_wait_s`, `tx_arm_s`, `tx_s`, `done_s`. The suffix
distinguishes a state literal from the signal, register or command constant it is named
after — `data_s` and `reg_wdata` are not the same kind of thing, and in a `case` arm the
suffix is what says so.

### B5 — Constants and generics are SCREAMING_SNAKE

`DESIGN_ID`, `CMD_WRITE`, `REG_LED_GLOBAL`, `STABLE_CYCLES`, `PERIOD`, `CLK_HZ`,
`DEBOUNCE_MS`.

This is enforced, but only after inverting VSG's default. `constant_004`, `generic_007` and
`generic_map_002` all ship with `case: lower`, which would have rewritten all 17 constants
and all 10 generic references to lower case. The config pins them to `upper` instead. See F3
— this is the one place where taking a VSG default at face value would have quietly destroyed
a convention the code holds perfectly.

### B6 — External names are frozen

`forgix_hello_world` and its ports `clk_32m`, `spi_cs_n`, `spi_sck`, `spi_sdio_in`,
`spi_sdio_out`, `spi_sdio_oe`, `button_n`, `led_r_n`, `led_g_n`, `led_b_n` are bound by pin
constraints outside the VHDL. They are lower-case snake_case and so already satisfy B1 and
B2; the rule here is that they may not be changed to satisfy anything else.

Port **order** in an entity declaration is frozen for the same class of reason: the three
positional port maps that C1 is about are correct today only because of the order they are
written in. Reordering ports and converting to named association are both safe; doing the
first without the second is a silent miswiring. So they are frozen until C1 is applied, and
after that there is no reason to reorder them anyway.

---

## C. Structure and layout

### C1 — Named association in every port map and generic map

**This is the largest readability defect in the tree and the reason this rubric exists.**

```vhdl
-- BAD: what is the eleventh signal bound to?
spi : entity work.forgix_spi port map (
  clk_32m, rst, spi_cs_n, spi_sck, spi_sdio_in, spi_sdio_out, spi_sdio_oe,
  wr, rd, addr, wdata, rdata, reset_regs, activity, spi_error);

-- GOOD
spi : entity work.forgix_spi
  port map (
    clk       => clk_32m,
    rst       => rst,
    cs_n      => spi_cs_n,
    ...
  );
```

Three files instantiate positionally: `forgix_hello_world.vhd` (31 formals across three
instantiations), `tb_pwm.vhd` (10) and `tb_spi_regs.vhd` (10). `tb_button.vhd` already uses
named association and is the model.

VSG detects this as `port_map_008` and **cannot fix it** — `fixable: false`. That is the
right shape for this rule. A formatter that guessed formal names from position would be
doing the one thing that makes positional maps dangerous, and doing it silently. So C1 gates
in check mode and is repaired by hand.

### C2 — One statement per line

Multi-statement lines are the current sources' second habit, and `forgix_spi.vhd` is where it
bites hardest:

```vhdl
-- BAD
state <= command_s; rx_count <= 0; tx_count <= 0; oe <= '0';
if command = CMD_READ then reg_read <= '1'; state <= read_wait_s;
else state <= data_s; end if;
```

Enforced by `sequential_007`, `case_012`, `if_020`, `if_022`, `if_024`,
`conditional_waveforms_001`, `assert_003`, `assert_005` and `report_statement_002`. All
fixable.

### C3 — One identifier per declaration line

```vhdl
-- BAD
signal command, address, rx_shift, tx_shift : byte_t := (others => '0');
signal r_scaled, g_scaled, b_scaled : unsigned(15 downto 0) := (others => '0');

-- GOOD
signal command  : byte_t := (others => '0');
signal address  : byte_t := (others => '0');
...
```

**Decided against the grouped-family exception.** `r_scaled, g_scaled, b_scaled` is the
strongest case for grouping — three signals of one type that are genuinely one idea — and it
is still declared one per line. The argument is the governing principle: "a family" is a
judgement, VSG cannot make it, and a rule that applies only where a human agrees it applies
is not a rule. One per line is mechanically decidable, so it is what the rubric says.

VSG's `signal_015` and `variable_015` default to `consecutive: 2`, which tolerates pairs and
would have left `signal rx_count, tx_count` and `variable rise, fall` alone. The config sets
both to `1`. `port_026` does the same for ports and has no threshold to set.

### C4 — Every process carries a label

Seven of the nine processes in the tree are unlabelled. A label is what turns a GHDL
assertion failure or a waveform hierarchy from a line number into a name.

`process_016` detects this and cannot fix it, for the same reason `port_map_008` cannot: the
label has to mean something. Naming these seven is the second of the two hand-edit jobs.

### C5 — Named ends

`end entity forgix_spi;`, `end architecture rtl;`, `end package forgix_pkg;`,
`end process debounce;`, `end procedure send_byte;`.

Enforced by `entity_019`, `architecture_024`, `package_014`, `process_018` and
`procedure_014`. All fixable **except** `process_018` where the process has no label yet —
which is why the seven `process_018` residuals pair exactly with the seven `process_016`
residuals. Label the process and the named end follows mechanically.

### C6 — Two-space indent, spaces only

VSG's default, and the tree's existing baseline. `fpga/vsg.yaml` states it explicitly under
`rule: global:` rather than inheriting it silently, because a default that is load-bearing
should be written down. (Verified honoured, not decorative: setting it to 4 nearly doubles
the violation count.)

### C7 — Maximum line length 100

Matching `firmware/.clang-format`'s `ColumnLimit: 100`, so both languages in the repo wrap at
the same column. VSG's `length_001` defaults to 120 at `Warning` severity; the config sets
100 and raises it to `Error` so it gates.

The longest line in the tree today is 93 columns, so this rule currently reports nothing. It
is a ratchet against the future, and it is set where it is because the statement-splitting of
C2 shortens far more lines than the named association of C1 lengthens.

### C8 — Alignment within a declarative block

Colons in port, generic, signal, variable and constant declarations; `:=` in defaults; `<=`
in sequential and concurrent assignment blocks; `=>` in port maps. Alignment is per
contiguous block, so a blank line starts a new alignment group.

Enforced by `architecture_026`, `entity_017`, `package_400`, `declarative_part_400`,
`process_400`, `subprogram_body_400`, `procedure_410`, `instantiation_010` and
`concurrent_006`. All fixable, and collectively they are the single largest mechanical win in
the reformat.

### C9 — Whitespace hygiene

No trailing whitespace, exactly one terminating newline, no tabs.

---

## D. Documentation

**Every rule in this section is human-judged.** VSG has no comment-content rules and no file
header rule, so nothing here is enforced by `fpga/vsg.yaml`, and a clean VSG run says nothing
about whether any of D1 to D4 is satisfied. Section G's table is where that is recorded
honestly.

The eight files currently contain **zero comments**. Not sparse — zero. Section D is
therefore the entire documentation debt of the FPGA tree, and it is the half of this rubric
that will take actual thought.

### D1 — A header comment on every design unit

Above the context clause of each file, a block comment saying what the unit is FOR: what it
does, what it assumes about the clock and reset, and what it is connected to.

### D2 — A header must add information

**This is the rule that matters, and it is inherited verbatim in spirit from the firmware
rubric's D2.** A header that restates the entity name is a violation, not a formality
satisfied.

```vhdl
-- BAD -- says nothing the entity name does not already say
-- forgix_button: the button module. Debounces the button.

-- GOOD -- says what the reader would otherwise get wrong
-- Debounces the front-panel button by requiring DEBOUNCE_MS of stability
-- before a level change is believed. `pressed` is the debounced level;
-- `press_strobe` is a single-cycle pulse on press only, so a consumer
-- counting presses must use the strobe and not an edge on `pressed`.
```

If nothing informative can be said about a unit, that is a signal the unit is either obvious
enough to need no header or badly factored — resolve it there, do not pad the header.

### D3 — Why-comments at the point they explain

The reason a piece of logic is shaped the way it is belongs beside the logic, not in the
header. The header says what the unit is for; the body says why it looks like this. Three
sites in the current tree need this and are named here so the adoption pass has a checklist:

- **`forgix_spi.vhd`, the three-stage synchronizers.** `sck_sync`, `cs_sync` and `io_sync` are
  three deep, not two, and `rise`/`fall` are derived from bits 2 and 1 rather than the
  freshest bits. Why the third stage exists, and why edge detection reads the settled end of
  the chain, is not deducible from the code.
- **`forgix_spi.vhd`, the `tx_arm_s` turnaround.** The state exists to hold `oe` low for one
  additional falling edge so the FPGA does not drive SDIO while the MCU still owns it.
  `tb_spi_regs.vhd` asserts this in two places; the RTL says nothing about it.
- **`forgix_hello_world.vhd`, the power-on-reset counter.** `por` counts to `x"FF"` and holds
  `rst` asserted until it arrives. Why 255 cycles, and what needs that long, is a design
  decision recorded nowhere.

### D4 — Declaration defaults that are power-up state say so

The `:= (others => '0')`, `:= '1'`, `:= x"20"` and `:= x"40"` initializers are not stylistic
tidiness. They are the FPGA's configuration-time state, and F1 disables two VSG rules to
protect them. A one-line comment on the declarative block saying that is what makes the next
reader — or the next tool configuration — leave them alone.

---

## E. Testbench profile

### E1 — Delta table

| Rule | tb |
|---|---|
| A1 context clauses | as written, plus `use std.env.all;` for `stop` |
| A2 one design unit pair | as written — entity is empty, architecture is `sim` |
| A3 filename matches unit | as written, with the `tb_` prefix |
| A4 analysis order | benches analyse last and are never depended on |
| B1 – B5 naming | as written |
| B6 frozen external names | n/a — nothing outside binds a bench |
| C1 named association | as written, and `tb_button.vhd` is already the model (E2) |
| C2 – C3, C5 – C9 | as written |
| C4 process labels | as written; `tb_pwm` and `tb_spi_regs` already label `stimulus` |
| D1 / D2 header comment | required, with the subject changed (E3) |
| D3 why-comments | required — the timing constants are the target (E4) |
| D4 defaults are power-up state | n/a — a bench's defaults are stimulus, not configuration |

### E2 — The benches are where positional association is worst

`tb_pwm.vhd` and `tb_spi_regs.vhd` each bind 10 formals positionally, and `tb_spi_regs.vhd`
binds the top-level entity — the one whose port order B6 freezes. A bench that miswires the
DUT does not fail loudly; it fails as a confusing assertion several hundred nanoseconds
later. `tb_button.vhd` already uses named association, in the same tree, written by the same
hand, which is the evidence that this is drift rather than a considered bench dialect.

### E3 — A bench header says what behaviour it guards

D2 with the subject changed. A bench's header should say what regression it exists to catch,
not what it stimulates — the stimulus is readable from the code, the intent is not.

```vhdl
-- BAD
-- Testbench for forgix_rgb_pwm. Drives the PWM and checks the outputs.

-- GOOD
-- Guards the PWM duty cycle against the 8x8 multiply being truncated at the
-- wrong end: red at 0xFF with brightness 0x80 must be lit for exactly 127 of
-- 256 phases, so an off-by-one in the r_scaled(15 downto 8) slice fails here
-- rather than as a dim LED on the bench.
```

### E4 — Assert messages already meet the bar and must survive the reformat

`"FPGA drove SDIO before MCU turnaround"`, `"zero blue intensity should remain off"`,
`"reset did not restore blue default"` — these say what broke and not merely that something
did. They are the one thing in the FPGA tree that is already documented to the standard D2
asks for, and the reformat must not disturb them. `assert_003` and `assert_005` move the
`report` and `severity` keywords onto their own lines; they do not touch the string.

The bare magic numbers beside them are the gap: `wait for 40 ns`, `wait for 3 us`,
`PERIOD / 2`, and the `CLK_HZ => 100_000` / `1_000` generic overrides that make debounce
periods short enough to simulate. Those are D3 sites.

---

## F. VSG rules disabled or reconfigured, and why

The config is `fpga/vsg.yaml`. Everything not listed here is VSG 3.35.0's default.

### F1 — Disabled for semantic safety

| Rule | Default demands | Why it is disabled |
|---|---|---|
| `signal_007` | `Remove default assignment.` (27 hits) | Signal declaration defaults are the FPGA's power-up state — the synchronizer idle levels, the PWM phase, `blue := x"20"`, `brightness := x"40"`, `led_enable := '1'`. Removing them changes what the device does at configuration time |
| `variable_007` | `Remove default assignment.` (1 hit) | Same for variables. `tb_pwm`'s `variable red_on, green_on, blue_on : natural := 0` counts from a defined start; without the default the duty-cycle asserts compare against `'U'` |

These two are the reason this config exists rather than a bare `vsg` invocation. Both are
`fixable: false` in VSG, so the danger was never that `--fix` would silently delete the
defaults — it is that 28 permanent violations would train everyone to ignore the output.

### F2 — Reconfigured because the default fights the design

| Rule | Default | Setting | Why |
|---|---|---|---|
| `instantiation_034` | `method: component` | `method: entity` | The tree uses direct entity instantiation. Converting would mean writing component declarations for every unit — a structural change, not a format. Not fixable, so the default would only produce permanent noise |
| `instantiation_036` | `action: add` | `action: remove` | Would pin `entity work.forgix_spi(rtl)`, converting default binding into explicit binding. Each entity has exactly one architecture, so the identifier adds nothing and freezes a choice the elaborator currently makes |
| `if_002` | `parenthesis: insert` | `parenthesis: remove` | Would add parentheses to all 27 if/elsif conditions. VHDL does not require them and no file in the tree uses them. Set to `remove` rather than disabled so the choice is enforced in both directions |

### F3 — Reconfigured because the default inverts an existing convention

| Rule | Default | Setting | Hits avoided |
|---|---|---|---|
| `constant_004` | `case: lower` | `case: upper` | 17 — every constant in `forgix_pkg.vhd`, plus `STABLE_CYCLES` and `PERIOD` |
| `generic_007` | `case: lower` | `case: upper` | 4 — `CLK_HZ`, `DEBOUNCE_MS` in two entities |
| `generic_map_002` | `case: lower` | `case: upper` | 6 — the same names at three instantiation sites |

All three are `fixable: true`, so leaving them at the default and running `--fix` would have
rewritten `DESIGN_ID` to `design_id` across the package. VHDL identifiers are case-insensitive,
so this would not have broken the build — which is precisely what makes it dangerous, and why
it is worth writing down. It would have been a silent, tree-wide, semantically invisible
reversal of B5, discovered only by reading the diff.

They were re-cased rather than disabled because `upper` is a no-op against the current
sources and turns B5 from a description into a gate.

**This was not visible from a plain `vsg` run.** VSG stops at the first phase that reports a
violation, and all three of these live in phases 4 to 6. The default run reports 379
violations, all phase 1, and none of them are these. `-ap` reports 743 and surfaces them. Any
future audit of this config must use `-ap`.

### F4 — Tightened

| Rule | Default | Setting | Why |
|---|---|---|---|
| `signal_015` | `consecutive: 2` | `consecutive: 1` | C3 is one per line, not up to two |
| `variable_015` | `consecutive: 2` | `consecutive: 1` | as above |
| `length_001` | `length: 120`, `Warning` | `length: 100`, `Error` | C7 |

---

## G. Conformance table

A green VSG run does **not** mean a file conforms. Section D is invisible to it, and section D
is currently 100% unsatisfied.

| Rule | Automated | Notes |
|---|---|---|
| A1 context clause order | no | VSG has no rule for it; "unused context clause" needs elaboration |
| A2 one design unit pair | no | |
| A3 filename matches unit | no | |
| A4 analysis order | no | encoded in `scripts/test.sh`, not in the source |
| B1 snake_case objects | yes | case only — `signal_004`, `variable_004`, `port_010`, `entity_008` and family enforce lower case; nothing checks the underscores |
| B2 `_n` active-low suffix | **no** | semantic — nothing can tell an active-low signal from its declaration |
| B3 `_t` type suffix | partial | `type_004` checks case, not suffix; a `suffix` rule exists but is off by default |
| B4 `_s` state literal suffix | no | as B3 |
| B5 SCREAMING_SNAKE constants | yes | `constant_004`, `generic_007`, `generic_map_002`, re-cased per F3 |
| B6 external names frozen | **no** | requires reading the `.isf` and the project XML |
| C1 named association | **detect only** | `port_map_008` gates it; `fixable: false`, so repair is manual |
| C2 one statement per line | yes | fixable |
| C3 one identifier per declaration | yes | fixable |
| C4 process labels | **detect only** | `process_016`; a machine cannot name a process meaningfully |
| C5 named ends | yes | fixable, except `process_018` on an unlabelled process — which C4 gates anyway |
| C6 two-space indent | yes | fixable |
| C7 100 columns | detect only | `length_001` is `fixable: false`; wrapping is a judgement |
| C8 alignment | yes | fixable |
| C9 whitespace hygiene | yes | fixable |
| D1 header comment present | **no** | VSG has no file-header rule |
| D2 header adds information | **no** | the most important rule here, and only a reviewer can judge it |
| D3 why-comments at the point | **no** | |
| D4 defaults documented as power-up state | **no** | |

C1 and C4 are the interesting rows. Both are fully automated as *gates* and fully manual as
*repairs*, which is the correct division: the machine is reliable at spotting a positional
map or an unlabelled process and would be actively harmful at fixing either.

---

## Adoption

Nothing has been applied. In order:

### 1. Mechanical

```bash
vsg -c fpga/vsg.yaml --fix -f fpga/rtl/*.vhd fpga/tb/*.vhd
```

Resolves **588 of 653** violations. No hand edits, no judgement calls.

Semantic neutrality of this step has been proved rather than asserted, on scratchpad copies:
all three testbenches pass against the formatted sources and stop at **identical simulated
times** — `tb_button` @2830 ns, `tb_pwm` @2577 ns, `tb_spi_regs` @45140 ns — matching the
unformatted baseline exactly. A token-level diff of the formatted output against the original
shows only: replicated declaration keywords and defaults from the C3 splits, per-port
`: in/out <type>;` from `port_026`, the C5 simple names, and `process (clk) is`. Every one of
the 15 ports of `forgix_spi` remains in its original position, and all 11 declaration defaults
survive — replicated across the split declarations, not dropped.

That is the same standard the firmware's mechanical pass was held to, where both target images
rebuilt byte-identical.

### 2. By hand

The remaining **65**, in commits grouped by concern:

| Work | Rule | Count | Where |
|---|---|---|---|
| Named association | `port_map_008` | 51 | `forgix_hello_world.vhd` 31, `tb_pwm.vhd` 10, `tb_spi_regs.vhd` 10 |
| Process labels | `process_016` | 7 | all files except `forgix_pkg.vhd`, `tb_pwm.vhd`, `tb_spi_regs.vhd` |
| Named `end process` | `process_018` | 7 | follows mechanically once labelled |

`forgix_pkg.vhd` reaches zero on the mechanical pass alone.

Then section D, which VSG cannot count and which is the larger job: headers on all eight
units, and why-comments at the sites named in D3 and E4. There is no violation count to
chase here, which is exactly why D2 is written the way it is.

### 3. CI

`vsg -c fpga/vsg.yaml -ap` over all eight files, with **`vsg==3.35.0` pinned** in the
workflow.

**The pin is what makes this gate acceptable, and the contrast with the firmware is the
point.** `scripts/format_firmware.sh --check` is deliberately *not* in CI: its config uses
clang-format options whose spelling and behaviour moved across versions —
`SpacesInParentheses` became `SpacesInParens` in 17, `Cpp11BracedListStyle` became an enum in
21 — and the runner's clang-format version is not pinned. A version mismatch would fail the
build over formatting that is locally correct, so the firmware tree gates on a Python checker
with no such dependency and accepts that pure-formatting drift can still occur between
formatter runs.

VHDL has no such excuse. VSG is a pinnable Python package, and this rubric's rule IDs, option
names and default values were all read out of 3.35.0 specifically — `signal_015`'s
`consecutive`, `if_002`'s `parenthesis`, `instantiation_034`'s `method` are exactly the kind
of option spelling that moved under clang-format. Pinning the version removes the objection
that kept the C formatter out of CI, so the VHDL formatter can be a real gate from the start
rather than a local convention that drifts.

Two operational notes for whoever writes the workflow step:

- Use `-ap`. Without it VSG stops at the first failing phase and a green-looking run can hide
  several hundred violations in later phases (F3).
- VSG exits non-zero when violations exist, so no `--check` flag or exit-code plumbing is
  needed; the plain run is the gate.
