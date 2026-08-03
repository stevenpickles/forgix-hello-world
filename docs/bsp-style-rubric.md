# BSP formatting rubric

Applies to `firmware/src/bsp/*.{h,c}` and the hand-written fakes in
`firmware/tests/support/mock_bsp_*.{h,c}`, which have been kept in lockstep with the package they
stand in for.

Derived from `../comms-v4-firmware/bsp/`, using `bsp_uarts.h` and `bsp_uarts.c` as the reference
pair. That repo carries the convention consistently across 19 files but has never written it down:
no `.clang-format`, no `.editorconfig`, no style guide, and no C linter enabled in its
`.trunk/trunk.yaml`. The convention survives there by copy-paste, which is why its newest files
carry the most drift. This document is the written form.

**Status: applied.** All 28 files score 100% on the automated rules. `scripts/check_bsp_style.py
--strict` runs in CI, so a regression fails the build. Reproduce the formatter half with
`scripts/format_bsp.sh`, or `--check` to verify without writing.

That score is not a claim that the BSP is well documented — D2, the rule that a summary must add
information, is the most important one here and no script can judge it. The checker confirms the
blocks exist.

Every rule has a stable ID. The checker cites these, so renumbering them breaks its output.

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

### C10 — Inline braced values are spaced: `{ 0 }`

A side effect rather than a preference. `SpacesInParentheses` also spaces braced initializer
lists, and no value of `Cpp11BracedListStyle` overrides it, so the inner-paren spacing of C1 and
tight `{0}` cannot both be had from clang-format. The comms-v4 reference writes them tight; forgix
deviates to keep the format machine-reproducible.

### C7 — Four spaces, no tabs

Continuation lines of a `/* … */` block comment align under the `/* ` with three extra leading
spaces.

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

## E. Deviations from the reference, and why

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

## F. Conformance table

A green checker run does **not** mean a file conforms. Roughly a third of the rubric needs a human.

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

The BSP does not conform today. Reformatting it is a separate, larger job than writing this down,
and adopting B5/B6 in particular would rename the package's types — struct typedefs gain `_t_tag`
tags, and **enum typedefs lose their `_t`**, so `bsp_boot_reason_t` becomes `bsp_boot_reason`. That
reaches application code, the fakes and the tests, and it partly reverses the "functions only"
scope of the earlier rename.

Done, in this order:

1. Everything a formatter can apply, via `scripts/format_bsp.sh`.
2. Naming and documentation by hand, in commits grouped by concern.
3. `scripts/check_bsp_style.py --strict` in `.github/workflows/ci.yml`.

**`scripts/format_bsp.sh --check` is deliberately not in CI.** The config uses options whose
spelling and behaviour changed across clang-format versions — `SpacesInParentheses` became
`SpacesInParens` in 17, `Cpp11BracedListStyle` became an enum in 21 — and the runner's version is
not pinned. A version mismatch would fail the build over formatting that is locally correct.
Adding it means pinning a clang-format version in the workflow first; until then the Python
checker, which has no such dependency, is what CI enforces.

### What conforming cost

Worth recording, because the rubric was written before any of it was applied and three rules did
not survive contact:

- **Multi-line initializer braces** stay on the `=` line (C6). No clang-format option breaks there.
- **Inline braced values** are `{ 0 }`, not the reference's `{0}` (C10). A side effect of the
  inner-paren spacing, not separately controllable.
- **Trailing `/*!<` comments at column 89** (C8) cannot coexist with a 100-column limit. The
  reference only manages it because nothing there enforces a limit. forgix uses a preceding block
  comment instead.

Each was resolved the same way: a rule a machine cannot reproduce is not a rule.
