# Firmware formatting rubric

Applies to the whole firmware tree, in three profiles:

| Profile | Files |
|---|---|
| **bsp** | `firmware/src/bsp/*.{h,c}`, and the hand-written fakes in `firmware/tests/support/mock_bsp_*.{h,c}` that have been kept in lockstep with the package they stand in for |
| **application** | `firmware/src/application/*.{h,c}`, `firmware/src/main.c`, `firmware/src/diagnostics/led_only_main.c` |
| **tests** | `firmware/tests/test_*.c` |

Sections A to D are the rules. They are stated throughout as the **bsp** profile has them, because
that is the profile they were derived from; section E then lists, rule by rule, where the other two
differ. A rule section E does not mention applies to all three unchanged, which is most of them —
the layers differ in what they call things, not in how they are laid out.

The rules are derived from `../comms-v4-firmware/bsp/`, using `bsp_uarts.h` and `bsp_uarts.c` as the reference
pair. That repo carries the convention consistently across 19 files but has never written it down:
no `.clang-format`, no `.editorconfig`, no style guide, and no C linter enabled in its
`.trunk/trunk.yaml`. The convention survives there by copy-paste, which is why its newest files
carry the most drift. This document is the written form.

> **Provenance only.** The `../comms-v4-firmware/bsp/` reference is cited above to explain where
> each rule came from and why. It is historical: the sibling repository is not required to exist
> to apply or check this rubric. `scripts/check_firmware_style.py` is self-contained and reads
> nothing outside this repository.

**Status: applied to all three profiles.** All 55 files in the firmware tree score 100% on the
automated rules — 34 in **bsp**, 15 in **application**, 6 in **tests**. `scripts/check_firmware_style.py
--strict` runs in CI over all of them by default, so a regression in any layer fails the build.
Reproduce the formatter half with `scripts/format_firmware.sh`, or `--check` to verify without
writing.

That score is not a claim that the firmware is well documented — D2, the rule that a summary must
add information, is the most important one here and no script can judge it. The checker confirms the
blocks exist.

Every rule has a stable ID. The checker cites these, so renumbering them breaks its output. That is
also why section E is a table of deltas rather than a second set of rules: one vocabulary, three
dialects.

---

## A. File organization

### A1 — Header skeleton

```c
#ifndef FORGIX_BSP_LED_H
#define FORGIX_BSP_LED_H


#ifdef __cplusplus
extern "C" {
#endif




/***************************************************************************************
**
** Compiler Include Directives
**
***************************************************************************************/


#include "bsp_types.h"




/***************************************************************************************
**
** Public Function Declarations
**
***************************************************************************************/


void BSP_LedOff( void );




#ifdef __cplusplus
}
#endif


#endif
```

### A2 — Source skeleton

A `.c` file opens directly with its first banner on line 1. No preamble, no file-header comment.

### A3 — Banner form

Exactly five lines, exactly **88 columns** on the first and last:

- line 1: `/` followed by 87 `*`
- line 2: `**`
- line 3: `** ` followed by the title, with no trailing padding and no closing `**`
- line 4: `**`
- line 5: 87 `*` followed by `/`

### A4 — Canonical section titles, headers

In this order:

1. `Compiler Include Directives`
2. `Compiler Define Directives`
3. `Enumerated Values, Type Definitions`
4. `Public Function Declarations`

### A5 — Canonical section titles, sources

In this order:

1. `Compiler Include Directives`
2. `Compiler Define Directives`
3. `Enumerated Values, Type Definitions`
4. `Private Variable Declarations`
5. `Private Function Declarations`
6. `Interrupt Handler Overrides` *(only in files that override a weak vendor symbol)*
7. `Public Function Definitions`
8. `Private Function Definitions`

The reference calls section 6 `Interrupt Handler Overrides` in `bsp_uarts.c` and
`HAL Callback Function Definitions` in `bsp_timers.c`. Since `bsp_uarts.c` is the designated
reference, this rubric takes its spelling.

Section 6 holds **every function whose name is imposed from outside** — vendor interrupt handlers
and weak-symbol overrides alike. Gathering them in one place is what makes B1 enforceable
everywhere else. In forgix that means `tud_cdc_rx_cb` and `tud_cdc_tx_complete_cb` in `bsp_usb.c`,
and `psram_eid_to_size` in `bsp_memory.c`, whose `size_t` return type the Pico SDK fixes.

### A6 — A section exists only if it has content

Do not emit an empty banner as a placeholder. The reference's `bsp_ethernet.c` keeps four empty
skeleton banners; that is a one-file habit and is not adopted. The relative order of the sections
that *are* present must still follow A4/A5.

### A7 — Blank-line rhythm

| Position | Blank lines |
|---|---|
| Between the guard `#define` and `#ifdef __cplusplus` | 2 |
| Between the opening guard block's `#endif` and the first banner | 4 |
| Before any mid-file banner | 4 |
| After a banner's closing line | 2 |
| Between function declarations in a header | 1 |
| Between function definitions in a source | 2 |
| Between typedef blocks | 2 |
| Between related single-line typedefs (e.g. a family of callback types) | 1 |
| Before the `_COUNT` sentinel in an enum | 1 |
| After the last declaration, before the closing `#ifdef __cplusplus` | 4 |
| Between the closing `}`/`#endif` and the final `#endif` | 2 |

### A8 — Include grouping

Own header first, then vendor and SDK headers, then C standard headers. One blank line between
groups. A `.c` includes its own header first; a header includes `"bsp_types.h"` for the type
vocabulary.

---

## B. Naming

### B1 — Public functions: `BSP_` + PascalCase

`BSP_LedSet`, `BSP_FpgaPing`, `BSP_WatchdogFeed`. Acronyms stay uppercase: `BSP_UsbHealth` is
acceptable, `BSP_FPGAPing` is not required but `BSP_UARTSetBaudRate` shows the reference's habit of
keeping multi-letter acronyms solid.

The fakes use `MOCK_BSP_` + PascalCase for their test-control helpers, and define the real `BSP_`
symbols unchanged.

**Exempt:** functions in the `Interrupt Handler Overrides` section (A5). Their names belong to a
vendor. Putting them anywhere else in the file makes them look like a rule violation, which is the
practical reason that section exists.

### B2 — Subsystem before verb

`BSP_LedSet`, not `BSP_SetLed`. The reference is split on this (`BSP_GetUARTFrameErrorCount` vs
`BSP_UARTGetRemainingDMATransferCount`); forgix is already consistently subsystem-first and stays
that way.

### B3 — Private functions: `_` + PascalCase

`_InitializeUART`, `_GetUARTIDFromInstanceAddress`. A file-local helper is `static` and carries the
leading underscore.

### B4 — File-scope variables: `_` + camelCase

`static uarts_t _uarts;`, `static uint32_t _markerWrites;`

### B5 — Struct typedefs: `<name>_t`, tag `<name>_t_tag`

```c
typedef struct uart_t_tag
{
    ...
} uart_t;
```

### B6 — Enum typedefs: `<name>`, no `_t`, tag `<name>_tag`

```c
typedef enum uart_id_tag
{
    ...
} uart_id;
```

The asymmetry with B5 is deliberate in the reference and is preserved here.

### B7 — Enum constants that escape the file: `BSP_` + SCREAMING_SNAKE

The prefix namespaces what leaves the translation unit, so it applies to enums declared in a
header. A file-local enum — a register map, a pin assignment — uses bare SCREAMING_SNAKE. The
reference agrees: its own file-local constants, such as `PC4_PORT` in `bsp_gpio.c`, carry no
prefix, while the public `uart_id` members are all `BSP_UART_ID_*`.

A counted enum starts with a `..._DUMMY = 0` guard value where zero is not a legal member, and ends
with a `..._COUNT` sentinel preceded by one blank line (A7). Both the sentinel and the last real
member carry a trailing comma.

### B8 — Function-pointer typedefs: `<module>_<event>_fn`

`uart_transmit_complete_fn`, `uart_idle_fn`. Variables, fields and parameters of such a type take
an `fn_` prefix: `fn_transmitCompleteCallback`, `fn_callback`.

### B9 — Pointers: `ptr_` prefix

Applies to parameters, struct fields and locals: `ptr_huart`, `ptr_data`, `ptr_uart`.

### B10 — Other locals: camelCase

`errorCode`, `instanceAddress`, `remainingTransferCount`.

### B11 — Include guards: `FORGIX_BSP_<NAME>_H`

**Deviation from the reference**, which uses `__bsp_uarts_h__`. Identifiers beginning with two
underscores are reserved to the implementation by C11 §7.1.3, so the reference's form is formally
undefined behaviour. It works in practice; forgix does not rely on it. The guard name must match
the filename. The closing `#endif` is bare, with no trailing comment.

---

## C. Spacing and layout

### C1 — Inner spacing inside parentheses

```c
void BSP_UARTSetBaudRate( const uint8_t id, const uint32_t baud_rate );
if ( ptr_data != NULL )
while ( count > 0u )
for ( uint32_t index = 0; index < count; ++index )
switch ( instanceAddress )
```

### C2 — `( void )` in declarations and definitions, `()` in zero-argument calls

```c
void BSP_LedOff( void );        /* declaration */
BSP_LedOff();                   /* call */
```

### C3 — Inner spacing inside subscripts

`me->uartList[ id ]`, `uint32_t snapshots[ BSP_WATCHDOG_SNAPSHOT_SLOTS ]`

### C4 — Casts: `(type) value`

No space inside the parentheses, one space after them: `(uint32_t) ptr_huart->Instance`,
`(void) status;`

### C5 — Pointer declarators: `type * const name`

A space on both sides of the `*`: `const uint8_t * const ptr_data`, `uarts_t * const me`.

### C6 — Allman braces

Opening brace on its own line for functions, control flow, and `struct`/`enum` definitions.
`else` and `else if` start their own line after the closing brace.

**Not** multi-line initializers: those keep their brace on the `=` line. clang-format has no
option for breaking there — braced lists answer to `Cpp11BracedListStyle`, which governs spacing
rather than placement, and `BraceWrapping` has no `AfterInitializer`. The rule was dropped rather
than maintained by hand, on the grounds that a rule a machine cannot reproduce is not a rule.

A compound literal is the same case, and the application layer is where it first came up:
`console = (console_state_t) {` is an initializer whose brace stays put. The `)` before it closes a
cast, not a parameter list — which is the distinction the checker has to make, since Allman is
about the brace that follows a signature or a condition.

### C10 — Inline braced values are spaced: `{ 0 }`

A side effect rather than a preference. `SpacesInParentheses` also spaces braced initializer
lists, and no value of `Cpp11BracedListStyle` overrides it, so the inner-paren spacing of C1 and
tight `{0}` cannot both be had from clang-format. The comms-v4 reference writes them tight; forgix
deviates to keep the format machine-reproducible.

### C7 — Four spaces, no tabs

Continuation lines of a `/* … */` block comment align under the `/* ` with three extra leading
spaces.

A continued *expression* is not indented to a nesting level either — it aligns under what it
continues, whichever side of the line break the operator falls on — trailing on the first line, or
leading the second, as clang-format decides:

```c
    packed |= ( diagnostics.fpga_reconfigures & HEALTH_FPGA_RECONFIGURE_MASK )
              << HEALTH_FPGA_RECONFIGURE_SHIFT;
```

The four-space rule governs nesting, and a run-on line is not a nesting level.

### C8 — Trailing declaration comments at column 89

```c
static uarts_t _uarts;                                                                  /*!< sole instance of UARTs */
```

Column 89 is one past the 88-column banner, so the comments form a straight edge against it.

### C9 — No trailing whitespace; exactly one terminating newline

---

## D. Documentation

### D1 — `/// <summary>` on every function definition

Including `static` ones. Three lines, tag on its own line, body indented four columns after
`/// `:

```c
/// <summary>
///     Reads the LED registers back from the FPGA rather than returning a
///     cached copy, so a stuck bus shows up as a readback mismatch.
/// </summary>
bsp_led_state_t BSP_LedGet( void )
```

### D2 — A summary must add information

**This is the rule forgix does not inherit from the reference.** A summary that restates the
function name is a violation, not a formality satisfied.

```c
/* BAD — says nothing the signature does not already say */
/// <summary>
///     Gets the noise error count for a specific UART.
/// </summary>
uint32_t BSP_GetUARTNoiseErrorCount( const uint8_t id );

/* GOOD — says what the caller would otherwise get wrong */
/// <summary>
///     Count of framing errors seen on this bus since boot. Latched by the
///     error interrupt, so it advances even while no read is outstanding.
/// </summary>
uint32_t BSP_GetUARTNoiseErrorCount( const uint8_t id );
```

If nothing informative can be said about a function, that is a signal the function is either
obvious enough to need no comment or badly named — resolve it there, do not pad the summary.

### D3 — `/// <returns>` on every non-void function

The reference applies this to 8 of 13 value-returning functions and is inconsistent *within*
`bsp_uarts.c` — the four adjacent error-count getters omit it while two functions fifty lines later
have it. This rubric resolves the ambiguity: always.

### D4 — Why-prose lives in the body

The reason a pull-up is swapped, the reason identity bytes are captured during detection, the
reason a register is read twice — these stay as `/* … */` block comments at the point in the body
they explain. The summary says what the function is for; the body comments say why the code is
shaped the way it is. Neither replaces the other.

### D5 — Headers carry no per-declaration doc comments

Documentation lives on the definition. Only `bsp_flash.h` deviates in the reference, and it is the
newest and driftiest file there.

### D6 — `<param>` tags are not required

Present only in `bsp_flash.c` in the reference and absent from the reference pair itself. Parameter
meaning belongs in the summary prose or in the parameter name.

---

## E. Layer profiles

A profile is A–D with a short list of named deltas. None of the deltas below was invented for this
document: each one records what its layer already does across every file in it. That is the whole
argument for having profiles at all — the application layer is not sloppy, it is consistent with
something other than the BSP, and a rename that made it consistent with the BSP would touch every
call site to buy nothing. Where a layer was genuinely inconsistent with itself, the rubric takes
the majority form and the reformat fixes the outlier.

### E1 — Delta table

| Rule | application | tests |
|---|---|---|
| A1 header skeleton | as written | n/a — no test headers |
| A2 source skeleton | as written; `led_only_main.c` is the one file with a preamble to move (E7) | as written |
| A3 banner form | as written | as written |
| A4 / A5 sections | as written, with the table placement rule in E10 | as written, with the placement rules in E6 and E10 |
| A6 / A7 | as written | as written |
| A8 include grouping | own header, C standard, project (E5) | `"unity.h"`, C standard, project (E5) |
| B1 public functions | `application_` + snake_case, plus `main` | `test_*`, `setUp`, `tearDown` (E2) |
| B2 subsystem before verb | as written: `application_console_set_echo`, not `application_set_console_echo` | n/a |
| B3 private functions | bare snake_case — `parse_byte`, `mark_write`, `step_psram` | bare snake_case — `poll_at`, `open_menu_at` |
| B4 file-scope variables | bare snake_case when mutable, SCREAMING_SNAKE when `static const` (E3) | same |
| B5 struct typedefs | `<name>_t`, **anonymous tag** (E4) | n/a — tests declare no types |
| B6 enum typedefs | `<name>_t`, **anonymous tag** (E4) | n/a |
| B7 enum constants in headers | `APPLICATION_` + SCREAMING_SNAKE | n/a |
| B8 function-pointer typedefs | `<module>_<event>_fn` as written (`ibit_run_fn`); no `fn_` prefix on fields or parameters | n/a |
| B9 `ptr_` prefix | not used | not used |
| B10 locals | snake_case, not camelCase | snake_case |
| B11 include guards | as written — already `FORGIX_<STEM>_H` throughout | n/a |
| C1 – C10 | as written | as written |
| D1 / D3 doc blocks | required, on definitions, including the two `main`s | **not required** (E8) |
| D2 a summary must add information | as written, and it is still the rule that matters | n/a |
| D4 why-prose in the body | as written | as written — this is what test files already have |
| D5 no `///` in headers | as written, and headers carry `/* … */` contract prose (E9) | n/a |
| D6 `<param>` tags | as written — not required | n/a |

One outlier was known and was a fix rather than a delta: `channelIndex`, the loop variable in
`application_effects.c`'s aurora blend, was the single camelCase local in the layer. It is now
`channel_index`, and it was the only rename the whole layer needed.

### E2 — Public names the layer does not own

`application_*` is the application's namespace and it is already applied without exception, so B1
needs only the prefix swapped. Two names sit outside it and cannot be renamed:

- `main`, in both `firmware/src/main.c` and `firmware/src/diagnostics/led_only_main.c`. The linker
  picks that name, not us.
- `setUp`, `tearDown` and every `test_*` in the test profile. Ceedling's generated runner calls
  them by name; `setUp`/`tearDown` are camelCase because Unity spells them that way.

This is the same exemption A5's `Interrupt Handler Overrides` section exists to make visible for the
BSP, arriving by a different route: there the vendor owns the name, here the toolchain does.

### E3 — File-scope variables split on mutability

The application writes its module state into one lower-case singleton per file — `console`, `ibit`,
`ui`, `diagnostics`, `effects` — and its constant tables in SCREAMING_SNAKE: `MENU`, `STEPS`,
`WHEEL`, `HEARTBEAT`, `AURORA`, `OUTCOME_TEXT`. The split is consistent across every file and it is
useful: at a glance, upper case at file scope is a table you may read and lower case is state
somebody mutates. B4 collapses both into `_camelCase`; the application profile keeps them apart.

Test files do the same thing for the same reason — `activity_starts` counts, `FAKE_ACTIVITY` does
not change.

### E4 — Enum typedefs keep their `_t`, and tags are anonymous

The BSP's B5/B6 asymmetry — structs end `_t`, enums do not — is inherited from the reference and is
deliberate there. The application does not have it: `console_state_t`, `status_mode_t`,
`ibit_step_t`, `application_ibit_outcome_t`, all `_t`, all with anonymous tags.

This is a documented deviation rather than a rename, and the reason is asymmetric cost.
`application_ibit_outcome_t` is a public type: it is the return type of every built-in test step,
it appears in the tests, and dropping its `_t` would edit every consumer to make one identifier
agree with a convention the reference itself calls an asymmetry. The tags are a separate matter —
none of these types is ever named as `struct X` or `enum X`, so the `_t_tag` spelling would add
fourteen identifiers that nothing refers to.

### E5 — Include order is per layer

B-side note, but it belongs here: A8 puts the C standard headers *last* for the BSP, after the
vendor and SDK ones. The application has no vendor headers to place and orders its groups the other
way:

1. the header this file implements, alone
2. C standard headers
3. project headers

Test files replace the first group with `"unity.h"` — a test implements no header, and Unity's is
the one include that must come first. Within each group the entries are alphabetical, with the
generated `mock_auto_*` fakes forming a run after the hand-written `mock_bsp_*` ones.

Grouping is hand-maintained. The config sets `SortIncludes: Never` on purpose: clang-format sorts
*within* a group and would happily merge two groups it cannot tell apart, and the group boundaries
here carry meaning a blank line is the only record of.

### E6 — Where test functions live

`setUp`, `tearDown` and the `test_*` functions go under `Public Function Definitions`. They are not
called from anywhere a reader can see, which makes this look wrong, and it is not: the generated
runner is a separate translation unit that calls all three by name, so they are as public as
anything in the BSP. The `static` helpers — `poll_at`, `open_menu_at`, the callbacks handed to
CMock — go under `Private Function Declarations` and `Private Function Definitions` as usual.

### E7 — `led_only_main.c`'s preamble

A2 says a `.c` file opens on its first banner with no preamble. `led_only_main.c` opens with ten
lines of file-header prose explaining what the image is for and why stdio is absent from it. The
prose is worth keeping and A2 is worth keeping, so the prose moves below the first banner as a
block comment, unchanged. No other source in the tree has a preamble.

### E8 — Test files carry no `<summary>` blocks

D1 requires a summary on every definition, D3 a `<returns>` on every non-void one, and the test
profile is exempt from both. The exemption follows from D2 rather than working around it: a test's
name is its summary. Given

```c
void test_menu_returns_to_the_ui_without_consulting_the_fpga( void )
```

there is no sentence a `<summary>` could add, and the sentence it would get is a restatement of the
name — which is exactly the failure D2 names as a violation rather than a formality satisfied. The
`static` helpers in a test file are exempt for the same reason: `poll_at` is not concealing
anything.

What test files do have, and keep, is D4. The `/* … */` blocks above the interesting tests explain
why the case exists and what regression it is standing guard over, which is the one thing the
function name cannot carry. Those are the comments the reformat must not disturb.

### E9 — Application headers document the contract

D5 says documentation lives on the definition, and the automated half of it — no `///` in a header
— holds for every profile. The application reads the rest of D5 differently: its headers carry
per-declaration `/* … */` prose, and a good deal of it. `application_diagnostics.h` spends eleven
lines on why `release_led` and `reclaim_led` must be called in pairs, and eight on why nothing may
call `BSP_WatchdogBootReason` again after start-up. `application_ui.h` spends seventeen on what an
activity is allowed to do inside `poll`.

That is contract prose, not documentation duplicated from the definition, and it is addressed to
the caller reading the header — which is the only place it can be read from. It stays. D5's
prohibition remains what the checker enforces: no `///` blocks in headers, so there is exactly one
place a `<summary>` can live.

### E10 — A table of private functions lives at the tail of `Private Function Declarations`

A5 puts file-scope data under `Private Variable Declarations`, which comes before
`Private Function Declarations`. A `static const` table whose initializer takes the *address of a
private function* cannot go there: C requires the declarations first, and at that point in the file
they have not been written yet.

The convention is that such a table sits at the **tail** of `Private Function Declarations`, after
the prototypes it binds. It is a declaration of the same private machinery the section is already
about, and the alternative — splitting the prototype list so the table can sit between the
prototypes it needs and the ones it does not — would be worse: it makes the section's contents
depend on an initializer several screens below.

The BSP never had occasion to need this, which is why A5 does not mention it. Where it applies:

| File | Tables |
|---|---|
| `application_effects.c` | `BLINKER`, `ADVANCED` |
| `application_ibit.c` | `STEPS`, `SEQUENCE`, `SOAK`, `SINGLE` |
| `application_ui.c` | `MENU` |
| `test_application_ui.c` | `FAKE_ACTIVITY` |

A table that binds nothing — `WHEEL`, `HEARTBEAT`, `AURORA`, `OUTCOME_TEXT` — is ordinary file-scope
data and stays under `Private Variable Declarations`, where A5 puts it.

---

## F. Deviations from the reference, and why

These are the bsp profile's departures from comms-v4, decided when A–D were written. Section E's
deltas are a different kind of thing — those are departures from A–D, decided by code that already
existed.

| Deviation | Reason |
|---|---|
| Flat `firmware/src/bsp/` rather than `bsp/include/` + `bsp/source/` | `scripts/check_firmware_layers.py:44-49` globs `bsp_*.h` in one directory and `firmware/CMakeLists.txt:56-65` lists sources by path |
| `FORGIX_BSP_LED_H` guards (B11) | `__bsp_led_h__` uses identifiers reserved by C11 §7.1.3 |
| The `me` idiom is not required | Found in `bsp_uarts.c` only — 1 of the reference's 3 stateful modules. A file habit, not house style. A module may use it; nothing checks for it |
| `BSP_ASSERT` / `BSP_DEFINE_THIS_MODULE` are not rubric rules | Functional, not formatting. forgix has no assert facility, and adding one is a design decision with its own tradeoffs, not a reformat. See below |
| Empty banner skeletons not adopted (A6) | `bsp_ethernet.c` only |
| `<param>` tags not required (D6) | `bsp_flash.c` only |

### Possible follow-up: an assert facility

The reference asserts every parameter at the top of every public function, with
`BSP_DEFINE_THIS_MODULE( "bsp_uarts.c" )` supplying the module identity and `__LINE__` locating the
failure. forgix has no equivalent. Adopting it would be a genuine improvement to the BSP's contract
enforcement, but it is a behavioural change — it needs a decision about what a failed assert does on
a board with no debugger attached, and it interacts with the 100% branch-coverage gate in
`firmware/project.yml:44-46`. Deliberately out of scope for a formatting rubric.

Worth knowing if that follow-up happens: the reference's own `bsp_assert.h` has an asymmetry —
under `#ifdef BSP_NASSERT`, `BSP_ASSERT` and `BSP_ASSERT_ID` compile out but `BSP_ERROR()` does not.

---

## G. Conformance table

A green checker run does **not** mean a file conforms. Roughly a third of the rubric needs a human.

The column says whether a rule is mechanically decidable, which is a property of the rule and not of
the profile. Section E's deltas change what a rule matches, not whether a script can match it, so
this table reads the same for all three profiles.

| Rule | Automated | Notes |
|---|---|---|
| A1 header skeleton | partial | guard, `extern "C"` presence and position |
| A3 banner form and width | yes | byte-exact |
| A4 / A5 section titles and order | yes | |
| A6 no empty sections | yes | |
| A7 blank-line rhythm | yes | |
| A8 include grouping | no | requires knowing which headers are vendor |
| B1 public function names | yes | |
| B2 subsystem before verb | no | semantic |
| B3 private function names | yes | |
| B4 file-scope variable names | yes | |
| B5 / B6 typedef and tag names | yes | |
| B7 enum constant names | yes | |
| B8 function-pointer typedef and `fn_` | partial | typedef name only |
| B9 `ptr_` prefix | no | needs type information |
| B10 camelCase locals | no | needs parsing |
| B11 include guard | yes | must match filename |
| C1 / C2 paren spacing | partial | control keywords and declarations |
| C3 subscript spacing | yes | |
| C4 cast spacing | no | `(uint8_t) x` is lexically indistinguishable from `(a + b) * c` |
| C5 pointer declarators | no | |
| C6 Allman braces | yes | clang-format applies it |
| C10 spaced braced values | yes | clang-format applies it |
| C7 four spaces, no tabs | yes | |
| C8 column 89 comments | yes | |
| C9 whitespace hygiene | yes | |
| D1 `<summary>` present | yes | |
| D2 summary adds information | **no** | the most important rule, and only a reviewer can judge it |
| D3 `<returns>` on non-void | yes | |
| D4 why-prose in body | no | |
| D5 no header doc comments | yes | |

---

## Adoption

The BSP did not conform when this was written. Reformatting it was a separate, larger job than
writing it down, and adopting B5/B6 in particular renamed the package's types — struct typedefs gain `_t_tag`
tags, and **enum typedefs lost their `_t`**, so `bsp_boot_reason_t` became `bsp_boot_reason`. That
reached application code, the fakes and the tests, and it partly reversed the "functions only"
scope of the earlier rename. It is also why the application profile does not adopt B6 (E4): the
same argument that made the BSP rename worth doing makes a second one, on types the BSP does not
own, worth skipping.

Done, in this order:

1. Everything a formatter can apply, via `scripts/format_firmware.sh`.
2. Naming and documentation by hand, in commits grouped by concern.
3. `scripts/check_firmware_style.py --strict` in `.github/workflows/ci.yml`.

The application and test layers followed the same three steps afterwards, and the checker's default
scope widened from the BSP alone to all three layers once they had. CI's command did not change —
it has always been the plain `--strict` run, so widening the default *is* the gate flip.

**`scripts/format_firmware.sh --check` gates in CI.** It was deliberately absent for as long as
the runner's clang-format version was unpinned: the config uses options whose spelling and
behaviour changed across versions — `SpacesInParentheses` became `SpacesInParens` in 17,
`Cpp11BracedListStyle` became an enum in 21 — so a version mismatch could fail the build over
formatting that is locally correct. The forgix-build image removed that failure mode by pinning
clang-format (18.x, the symlinked `clang-format-18`), and the check joined `ci.yml`'s verify job.
A locally installed LLVM may still disagree with the pin; format with the container, or point
`CLANG_FORMAT` at a matching version. Bumping the image's clang-format major is a deliberate act
that ships with a full-tree reformat in the same change.

### What conforming cost — the bsp profile

Worth recording, because the rubric was written before any of it was applied and three rules did
not survive contact:

- **Multi-line initializer braces** stay on the `=` line (C6). No clang-format option breaks there.
- **Inline braced values** are `{ 0 }`, not the reference's `{0}` (C10). A side effect of the
  inner-paren spacing, not separately controllable.
- **Trailing `/*!<` comments at column 89** (C8) cannot coexist with a 100-column limit. The
  reference only manages it because nothing there enforces a limit. forgix uses a preceding block
  comment instead.

Each was resolved the same way: a rule a machine cannot reproduce is not a rule.

### What conforming cost — the application and test profiles

The BSP had already absorbed the rules that did not survive contact, so this half cost effort rather
than revisions. In order:

- **A mechanical clang-format pass over 21 files** — 13 in `src/application`, the 6 test files,
  `main.c` and `led_only_main.c`. No hand edits. Semantic neutrality was proved rather than
  asserted: both target images rebuilt byte-identical to the pre-format baseline, all 140 Ceedling
  tests passed, and coverage held at 100% line and branch. It held for every commit after it too.
- **A structural pass**, which is where the work actually was: banners and canonical sections
  throughout, `extern "C"` wrappers on the application headers, and 110 `static` prototypes written
  out — the layer had been declaring private functions by defining them in call order, so giving it
  `Private Function Declarations` meant creating the declarations that section is named after.
  E10 is the one convention this turned up that A–D had no answer for.
- **One rename**: `channelIndex` → `channel_index`. B10's camelCase locals are a bsp-profile rule
  and E1 does not carry them over, so exactly one identifier in the layer was in the wrong dialect.
  Compare the BSP, where B5/B6 renamed the package's types.
- **130 `/// <summary>` blocks**, on every application definition. The test files got none, and E8
  is the argument for why that is the honest answer rather than the lazy one.
- **Three fixes to the checker**, for false positives the new layers exposed and the BSP never
  could: `[` inside a string literal read as a subscript, a multi-line compound literal's brace read
  as a function brace, and a continuation aligned under an operator that started its own line. All
  three were the checker disagreeing with conforming code — verified by re-running the BSP report
  before and after and diffing it byte for byte.

Then the checker's default scope widened from the BSP to all three layers, which flipped the CI gate
without touching `ci.yml`.

### The drift lesson

While this work was in flight, the BSP was found to have drifted out of its own fixed point.
`bsp_adc.c` and `bsp_mcu.c` were edited by an earlier PR without re-running the formatter, and
nothing gated it, so the drift sat in `main` until a `--check` run during this effort turned it up.
Whitespace only, and restored in a single commit — but it had been there a while.

The widened gate closes part of this: structure, naming and documentation are now checked in CI for
every layer, not just the BSP. It does not close all of it. `format_firmware.sh --check` remains
local-only for the reason recorded above — the runner's clang-format version is not pinned, and that
decision has not changed — so drift of the pure-formatting kind is still possible between formatter
runs. The honest statement is that CI now catches everything the Python checker can decide, and the
gap is exactly the set of rules only clang-format can.
