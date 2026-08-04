#!/usr/bin/env python3
"""Score each firmware layer against its profile in docs/firmware-style-rubric.md.

Section E splits the rubric into three profiles -- bsp, application, tests -- that share every
layout rule and differ on naming and documentation. A profile here is those deltas as data; the
checks themselves are written once.

The default scope is the layers that conform today, which is the bsp profile, so a plain run is the
gate CI enforces with --strict. --layer widens the scope to a layer that does not conform yet,
where the run is advisory and reads as a work list. When the remaining layers conform, the default
scope becomes all of them and the gate moves with it.

Only the mechanically decidable rules are implemented. The rubric's conformance table marks which
those are; the rest need a reviewer, and the most important rule of all -- D2, that a summary must
say something the signature does not -- is one of them. A clean run here is not a conforming file.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import argparse
import re
import sys


ROOT = Path(__file__).resolve().parent.parent

BANNER_WIDTH = 88
BANNER_OPEN = re.compile(r"^/\*{5,}$")
BANNER_CLOSE = re.compile(r"^\*{5,}/$")
BANNER_TITLE = re.compile(r"^\*\* (.+)$")

HEADER_SECTIONS = (
    "Compiler Include Directives",
    "Compiler Define Directives",
    "Enumerated Values, Type Definitions",
    "Public Function Declarations",
)
SOURCE_SECTIONS = (
    "Compiler Include Directives",
    "Compiler Define Directives",
    "Enumerated Values, Type Definitions",
    "Private Variable Declarations",
    "Private Function Declarations",
    "Interrupt Handler Overrides",
    "Public Function Definitions",
    "Private Function Definitions",
)

# A definition is a line starting in column 0 that closes its parameter list, whose next
# meaningful line is a lone opening brace. Allman makes this reliable without a real parser.
SIGNATURE = re.compile(r"^([A-Za-z_][\w \t*]*?[\w*])\s*\(([^;]*)\)\s*$")
DECLARATION = re.compile(r"^([A-Za-z_][\w \t*]*?[\w*])\s*\(([^;]*)\)\s*;\s*$")
CONTROL = re.compile(r"^\s*(if|while|for|switch)\b")
TYPEDEF_CLOSE = re.compile(r"^\}\s*([A-Za-z_]\w*)\s*;")
TYPEDEF_OPEN = re.compile(r"^typedef\s+(struct|enum)\b\s*(\w+)?\s*$")
ATTRIBUTE = re.compile(r"^\s*(?:__attribute__|__weak\b|__used\b|__STATIC_INLINE\b)")
# A line ending in a binary operator runs on, so the next line aligns rather than nests.
CONTINUATION_TAIL = re.compile(r"(?:&&|\|\||[+\-*/%&|^<>=!]=?)$")

# Functions here answer to a vendor's prototype, so BSP_PascalCase does not apply to them.
VENDOR_SECTION = "Interrupt Handler Overrides"


@dataclass(frozen=True)
class Violation:
    rule: str
    line: int
    message: str


@dataclass(frozen=True)
class Rule:
    """A name that must match, and the shape to name in the message when it does not."""

    pattern: re.Pattern[str]
    shape: str

    def rejects(self, name: str) -> bool:
        return not self.pattern.match(name)


@dataclass(frozen=True)
class Profile:
    """Section E's per-layer deltas as data. Everything absent from here is shared by all layers."""

    name: str
    public_function: Rule
    private_function: Rule
    # E3 reads file scope as a split on mutability, so a static const table answers to its own
    # rule. The bsp profile points both halves at the same one, which is B4 unchanged.
    private_variable: Rule
    constant_variable: Rule
    enum_constant: Rule
    struct_typedef: Rule
    enum_typedef: Rule
    # "named" wants B5/B6's <name>_tag; "anonymous" is E4, where nothing ever says struct X.
    typedef_tag: str
    # E8: a test's name is its summary, so D1 and D3 do not apply to the tests profile at all.
    requires_docs: bool


BSP_PROFILE = Profile(
    name="bsp",
    public_function=Rule(re.compile(r"^(?:MOCK_)?BSP_[A-Z][A-Za-z0-9]*$"), "BSP_PascalCase"),
    private_function=Rule(re.compile(r"^_[A-Z][A-Za-z0-9_]*$"), "_PascalCase"),
    private_variable=Rule(re.compile(r"^_[a-z][A-Za-z0-9]*$"), "_camelCase"),
    constant_variable=Rule(re.compile(r"^_[a-z][A-Za-z0-9]*$"), "_camelCase"),
    enum_constant=Rule(re.compile(r"^(?:MOCK_)?BSP_[A-Z0-9_]+$"), "BSP_SCREAMING_SNAKE"),
    struct_typedef=Rule(re.compile(r"^\w+_t$"), "must end in _t"),
    enum_typedef=Rule(re.compile(r"^(?!\w*_t$)\w+$"), "must not end in _t"),
    typedef_tag="named",
    requires_docs=True,
)

APPLICATION_PROFILE = Profile(
    name="application",
    # E2: main belongs to the linker, so the namespace rule cannot reach it.
    public_function=Rule(re.compile(r"^(?:main|application[a-z0-9_]*)$"), "application_snake_case, or main"),
    private_function=Rule(re.compile(r"^[a-z][a-z0-9_]*$"), "snake_case"),
    private_variable=Rule(re.compile(r"^[a-z][a-z0-9_]*$"), "snake_case"),
    constant_variable=Rule(re.compile(r"^[A-Z][A-Z0-9_]*$"), "SCREAMING_SNAKE"),
    enum_constant=Rule(re.compile(r"^APPLICATION_[A-Z0-9_]+$"), "APPLICATION_SCREAMING_SNAKE"),
    # E4: both kinds end _t here, and neither carries a tag.
    struct_typedef=Rule(re.compile(r"^[a-z][a-z0-9_]*_t$"), "must be snake_case ending in _t"),
    enum_typedef=Rule(re.compile(r"^[a-z][a-z0-9_]*_t$"), "must be snake_case ending in _t"),
    typedef_tag="anonymous",
    requires_docs=True,
)

TESTS_PROFILE = Profile(
    name="tests",
    # E2: Ceedling's generated runner calls these three by name, so the toolchain owns them the way
    # a vendor owns what sits under Interrupt Handler Overrides.
    public_function=Rule(
        re.compile(r"^(?:test_[a-z0-9_]+|setUp|tearDown|main)$"), "test_snake_case, setUp or tearDown"
    ),
    private_function=Rule(re.compile(r"^[a-z][a-z0-9_]*$"), "snake_case"),
    private_variable=Rule(re.compile(r"^[a-z][a-z0-9_]*$"), "snake_case"),
    constant_variable=Rule(re.compile(r"^[A-Z][A-Z0-9_]*$"), "SCREAMING_SNAKE"),
    enum_constant=Rule(re.compile(r"^[A-Z][A-Z0-9_]*$"), "SCREAMING_SNAKE"),
    struct_typedef=Rule(re.compile(r"^[a-z][a-z0-9_]*_t$"), "must be snake_case ending in _t"),
    enum_typedef=Rule(re.compile(r"^[a-z][a-z0-9_]*_t$"), "must be snake_case ending in _t"),
    typedef_tag="anonymous",
    requires_docs=False,
)

PROFILES = {profile.name: profile for profile in (BSP_PROFILE, APPLICATION_PROFILE, TESTS_PROFILE)}


class Checker:
    """Accumulates violations for one file and remembers which rules were exercised."""

    def __init__(self, path: Path, text: str, profile: Profile) -> None:
        self.path = path
        self.text = text
        self.profile = profile
        self.lines = text.split("\n")
        if self.lines and self.lines[-1] == "":
            self.lines.pop()
        self.is_header = path.suffix == ".h"
        self.violations: list[Violation] = []
        self.applied: set[str] = set()
        self.banners = self._find_banners()
        self.comment_lines = self._block_comment_lines()

    # -- helpers ---------------------------------------------------------------------------

    def fail(self, rule: str, index: int, message: str) -> None:
        self.violations.append(Violation(rule, index + 1, message))

    def apply(self, *rules: str) -> None:
        self.applied.update(rules)

    def _find_banners(self) -> list[tuple[int, int, str]]:
        """Return (start_index, end_index, title) for each well-formed 5-line banner."""
        found = []
        for index, line in enumerate(self.lines):
            if not BANNER_OPEN.match(line):
                continue
            if index + 4 >= len(self.lines):
                continue
            title = BANNER_TITLE.match(self.lines[index + 2])
            if not BANNER_CLOSE.match(self.lines[index + 4]) or not title:
                continue
            found.append((index, index + 4, title.group(1)))
        return found

    def _block_comment_lines(self) -> set[int]:
        """Line indices sitting inside a /* */ block, whose indentation is alignment not nesting."""
        inside = set()
        open_block = False
        for index, line in enumerate(self.lines):
            if open_block:
                inside.add(index)
            cursor = 0
            while cursor < len(line):
                if open_block:
                    end = line.find("*/", cursor)
                    if end < 0:
                        break
                    open_block = False
                    cursor = end + 2
                else:
                    start = line.find("/*", cursor)
                    if start < 0:
                        break
                    open_block = True
                    cursor = start + 2
        return inside

    def _section_at(self, index: int) -> str | None:
        """Title of the banner section containing this line, if any."""
        title = None
        for start, _end, name in self.banners:
            if start > index:
                break
            title = name
        return title

    def _definitions(self) -> list[tuple[int, str, str]]:
        """(index, return_type_and_name, name) for every Allman function definition."""
        found = []
        for index, line in enumerate(self.lines):
            match = SIGNATURE.match(line)
            if not match:
                continue
            nxt = index + 1
            while nxt < len(self.lines) and self.lines[nxt].strip() == "":
                nxt += 1
            if nxt >= len(self.lines) or self.lines[nxt].strip() != "{":
                continue
            head = match.group(1).strip()
            name = head.split()[-1].lstrip("*")
            found.append((index, head, name))
        return found

    # -- rules -----------------------------------------------------------------------------

    def check_whitespace(self) -> None:
        self.apply("C7-tabs", "C9-trailing", "C9-newline")
        for index, line in enumerate(self.lines):
            if "\t" in line:
                self.fail("C7-tabs", index, "tab character; the BSP indents with four spaces")
            if line != line.rstrip():
                self.fail("C9-trailing", index, "trailing whitespace")
        if not self.text.endswith("\n"):
            self.fail("C9-newline", len(self.lines) - 1, "file does not end with a newline")
        elif self.text.endswith("\n\n"):
            self.fail("C9-newline", len(self.lines) - 1, "file ends with more than one newline")

    def check_indent(self) -> None:
        self.apply("C7-indent")
        depth = 0
        continued = False
        for index, line in enumerate(self.lines):
            stripped = line.strip()
            if not stripped or index in self.comment_lines:
                continue
            if not continued and line.startswith(" "):
                indent = len(line) - len(line.lstrip(" "))
                if indent % 4 != 0:
                    self.fail("C7-indent", index, f"indent of {indent} is not a multiple of four")
            code = stripped.split("/*")[0].split("//")[0].rstrip()
            depth = max(0, depth + code.count("(") - code.count(")"))
            # A run-on statement indents to align with what it continues, not to a nesting level.
            continued = depth > 0 or bool(CONTINUATION_TAIL.search(code))

    def check_allman(self) -> None:
        self.apply("C6-allman")
        for index, line in enumerate(self.lines):
            if index in self.comment_lines:
                continue
            code = line.split("/*")[0].split("//")[0].rstrip()
            if re.search(r"\)\s*\{$", code):
                self.fail("C6-allman", index, "opening brace shares the line; Allman puts it below")
            if re.search(r"\}\s*else\b", code) or re.search(r"\belse\s*\{$", code):
                self.fail("C6-allman", index, "else must start its own line with its brace below")

    def check_banner_form(self) -> None:
        self.apply("A3-banner")
        for index, line in enumerate(self.lines):
            if not (BANNER_OPEN.match(line) or BANNER_CLOSE.match(line)):
                continue
            if len(line) != BANNER_WIDTH:
                self.fail("A3-banner", index, f"banner rule is {len(line)} columns, must be {BANNER_WIDTH}")
        for start, _end, _title in self.banners:
            for offset in (1, 3):
                if self.lines[start + offset] != "**":
                    self.fail("A3-banner", start + offset, "banner padding line must be exactly '**'")

    def check_sections(self) -> None:
        self.apply("A4-A5-titles", "A4-A5-order", "A6-empty")
        allowed = HEADER_SECTIONS if self.is_header else SOURCE_SECTIONS
        if not self.banners:
            return
        seen = []
        for position, (start, end, title) in enumerate(self.banners):
            if title not in allowed:
                self.fail("A4-A5-titles", start + 2, f"'{title}' is not a canonical section title")
                continue
            seen.append((allowed.index(title), start))
            following = self.banners[position + 1][0] if position + 1 < len(self.banners) else len(self.lines)
            body = [line for line in self.lines[end + 1:following] if line.strip()]
            if not body:
                self.fail("A6-empty", start + 2, f"section '{title}' has no content; omit the banner")
        for earlier, later in zip(seen, seen[1:]):
            if later[0] < earlier[0]:
                self.fail("A4-A5-order", later[1] + 2, f"section is out of canonical order")

    def check_banner_spacing(self) -> None:
        self.apply("A7-before-banner", "A7-after-banner")
        for position, (start, end, title) in enumerate(self.banners):
            if start == 0:
                continue
            before = 0
            cursor = start - 1
            while cursor >= 0 and self.lines[cursor].strip() == "":
                before += 1
                cursor -= 1
            if cursor >= 0 and before != 4:
                self.fail("A7-before-banner", start, f"{before} blank lines before banner, expected 4")
            after = 0
            cursor = end + 1
            while cursor < len(self.lines) and self.lines[cursor].strip() == "":
                after += 1
                cursor += 1
            if cursor < len(self.lines) and after != 2:
                self.fail("A7-after-banner", end, f"{after} blank lines after banner, expected 2")

    def check_guard(self) -> None:
        if not self.is_header:
            return
        self.apply("B11-guard", "A1-extern-c")
        expected = "FORGIX_" + self.path.stem.upper() + "_H"
        if f"#ifndef {expected}" not in self.text or f"#define {expected}" not in self.text:
            self.fail("B11-guard", 0, f"include guard must be {expected}, matching the filename")
        if 'extern "C" {' not in self.text or "#ifdef __cplusplus" not in self.text:
            self.fail("A1-extern-c", 0, 'header must wrap its declarations in extern "C"')

    def check_spacing(self) -> None:
        self.apply("C1-paren", "C2-void", "C3-subscript", "C8-column")
        for index, line in enumerate(self.lines):
            if index in self.comment_lines:
                continue
            code = line.split("/*")[0].split("//")[0]
            if CONTROL.match(code) and re.search(r"\b(if|while|for|switch)\s*\((?! )", code):
                self.fail("C1-paren", index, "control statement needs a space inside its parentheses")
            if re.search(r"\(void\)\s*$", code.rstrip()) and (DECLARATION.match(line) or SIGNATURE.match(line)):
                self.fail("C2-void", index, "empty parameter list is written ( void ) when declared")
            if re.search(r"\[(?![ \]])", code) or re.search(r"(?<![ \[])\]", code):
                self.fail("C3-subscript", index, "subscript needs spaces inside the brackets")
            marker = line.find("/*!<")
            if marker >= 0 and marker != BANNER_WIDTH:
                self.fail("C8-column", index, f"trailing doc comment at column {marker + 1}, expected 89")

    def check_names(self) -> None:
        self.apply("B1-public", "B3-private-fn", "B4-private-var")
        for index, head, name in self._definitions():
            if head.startswith("static "):
                rule = self.profile.private_function
                if rule.rejects(name):
                    self.fail("B3-private-fn", index, f"static function '{name}' needs {rule.shape}")
            elif self._section_at(index) == VENDOR_SECTION:
                continue  # the vendor owns these names; that is what the section is for
            else:
                rule = self.profile.public_function
                if rule.rejects(name):
                    self.fail("B1-public", index, f"public function '{name}' needs {rule.shape}")
        for index, line in enumerate(self.lines):
            match = re.match(r"^static\s+(const\s+)?[\w ]+?[\w*]\s+(\w+)\s*(?:\[[^\]]*\])?\s*(?:=.*)?;", line)
            if not match:
                continue
            # E3 reads the const as the declaration's own statement about which half it is in.
            rule = self.profile.constant_variable if match.group(1) else self.profile.private_variable
            if rule.rejects(match.group(2)):
                self.fail("B4-private-var", index, f"file-scope '{match.group(2)}' needs {rule.shape}")

    def check_typedefs(self) -> None:
        self.apply("B5-B6-typedef", "B7-enum-constant")
        for index, line in enumerate(self.lines):
            opening = TYPEDEF_OPEN.match(line)
            if not opening:
                continue
            kind, tag = opening.group(1), opening.group(2)
            close = next(
                (i for i in range(index, len(self.lines)) if TYPEDEF_CLOSE.match(self.lines[i])), None
            )
            if close is None:
                continue
            name = TYPEDEF_CLOSE.match(self.lines[close]).group(1)
            rule = self.profile.struct_typedef if kind == "struct" else self.profile.enum_typedef
            if rule.rejects(name):
                self.fail("B5-B6-typedef", close, f"{kind} typedef '{name}' {rule.shape}")
            if self.profile.typedef_tag == "named":
                if tag != f"{name}_tag":
                    found = tag or "none"
                    self.fail("B5-B6-typedef", index, f"{kind} tag must be '{name}_tag', found '{found}'")
            elif tag:
                self.fail("B5-B6-typedef", index, f"{kind} tag must be anonymous, found '{tag}'")
            # B7 namespaces what escapes the translation unit. A file-local enum does not, and
            # the reference agrees -- its own file-local constants (PC4_PORT) carry no prefix.
            if kind != "enum" or not self.is_header:
                continue
            for member in range(index, close):
                constant = re.match(r"^\s{4}(\w+)\s*(?:=|,)", self.lines[member])
                rule = self.profile.enum_constant
                if constant and rule.rejects(constant.group(1)):
                    self.fail("B7-enum-constant", member, f"'{constant.group(1)}' needs {rule.shape}")

    def check_documentation(self) -> None:
        if self.is_header:
            self.apply("D5-header-docs")
            for index, line in enumerate(self.lines):
                if line.lstrip().startswith("///"):
                    self.fail("D5-header-docs", index, "documentation belongs on the definition, not here")
            return
        if not self.profile.requires_docs:
            return  # E8: the name of a test is the only summary it could honestly carry
        self.apply("D1-summary", "D3-returns")
        for index, head, name in self._definitions():
            cursor = index - 1
            # A placement attribute may sit between the doc block and the signature.
            while cursor >= 0 and ATTRIBUTE.match(self.lines[cursor]):
                cursor -= 1
            block = []
            while cursor >= 0 and self.lines[cursor].lstrip().startswith("///"):
                block.append(self.lines[cursor])
                cursor -= 1
            if not any("<summary>" in entry for entry in block):
                self.fail("D1-summary", index, f"'{name}' has no /// <summary> block")
            tokens = [token for token in head.split() if token not in ("static", "inline", "extern")]
            returns_value = not (tokens and tokens[0] == "void" and not tokens[-1].startswith("*"))
            if returns_value and not any("<returns>" in entry for entry in block):
                self.fail("D3-returns", index, f"'{name}' returns a value and needs /// <returns>")

    def check_declaration_spacing(self) -> None:
        if not self.is_header:
            return
        self.apply("A7-declarations")
        previous = None
        for index, line in enumerate(self.lines):
            if not DECLARATION.match(line):
                continue
            if previous is not None:
                gap = index - previous - 1
                if gap and gap != 1 and all(not self.lines[i].strip() for i in range(previous + 1, index)):
                    self.fail("A7-declarations", index, f"{gap} blank lines between declarations, expected 1")
            previous = index

    def run(self) -> None:
        self.check_whitespace()
        self.check_indent()
        self.check_allman()
        self.check_banner_form()
        self.check_sections()
        self.check_banner_spacing()
        self.check_guard()
        self.check_spacing()
        self.check_names()
        self.check_typedefs()
        self.check_documentation()
        self.check_declaration_spacing()

    @property
    def score(self) -> tuple[int, int]:
        failed = {violation.rule for violation in self.violations}
        return len(self.applied - failed), len(self.applied)


@dataclass(frozen=True)
class Scope:
    """One directory, the sources in it that belong to a layer, and the profile they answer to."""

    root: Path
    globs: tuple[str, ...]
    profile: Profile


FIRMWARE = ROOT / "firmware"
BSP_GLOBS = ("bsp*.h", "bsp*.c", "mock_bsp*.h", "mock_bsp*.c")
APPLICATION_GLOBS = ("application*.h", "application*.c", "main.c", "*_main.c")

# What each layer is made of. The bsp entry is the default scope: the layers below it do not
# conform yet, so widening the default is the commit that follows their reformat, not this one.
LAYERS = {
    "bsp": (
        Scope(FIRMWARE / "src" / "bsp", BSP_GLOBS, BSP_PROFILE),
        Scope(FIRMWARE / "tests" / "support", BSP_GLOBS, BSP_PROFILE),
    ),
    "application": (
        Scope(FIRMWARE / "src" / "application", ("application*.h", "application*.c"), APPLICATION_PROFILE),
        Scope(FIRMWARE / "src", ("main.c",), APPLICATION_PROFILE),
        Scope(FIRMWARE / "src" / "diagnostics", ("*_main.c",), APPLICATION_PROFILE),
    ),
    "tests": (Scope(FIRMWARE / "tests", ("test_*.c",), TESTS_PROFILE),),
}

# The globs --root scans, chosen by --profile, so a directory outside the table is still reachable.
PROFILE_GLOBS = {"bsp": BSP_GLOBS, "application": APPLICATION_GLOBS, "tests": ("test_*.c",)}


def discover(scopes: list[Scope]) -> list[tuple[Path, Profile]]:
    """Every source a scope claims, one profile per file, first claim winning."""
    found: dict[Path, Profile] = {}
    for scope in scopes:
        if not scope.root.exists():
            continue
        for pattern in scope.globs:
            for path in scope.root.rglob(pattern):
                found.setdefault(path, scope.profile)
    return sorted(found.items())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--layer",
        action="append",
        choices=("bsp", "application", "tests", "all"),
        help="layer to scan, repeatable; defaults to bsp, the scope that conforms today",
    )
    parser.add_argument(
        "--root", type=Path, action="append", help="directory to scan instead of a layer; repeatable"
    )
    parser.add_argument(
        "--profile",
        choices=tuple(PROFILES),
        default="bsp",
        help="profile and file globs to apply to --root directories (default: bsp)",
    )
    parser.add_argument("--strict", action="store_true", help="exit non-zero when a file violates a rule")
    parser.add_argument("--quiet", action="store_true", help="print only the summary line")
    arguments = parser.parse_args()

    if arguments.root:
        profile = PROFILES[arguments.profile]
        scopes = [Scope(root, PROFILE_GLOBS[arguments.profile], profile) for root in arguments.root]
    else:
        names = arguments.layer or ["bsp"]
        if "all" in names:
            names = list(LAYERS)
        scopes = [scope for name in names for scope in LAYERS[name]]

    files = discover(scopes)
    if not files:
        print(f"No sources found under {', '.join(str(scope.root) for scope in scopes)}", file=sys.stderr)
        return 1

    total_violations = 0
    passed_rules = 0
    total_rules = 0
    for path, profile in files:
        checker = Checker(path, path.read_text(encoding="utf-8"), profile)
        checker.run()
        good, applicable = checker.score
        passed_rules += good
        total_rules += applicable
        total_violations += len(checker.violations)
        if arguments.quiet:
            continue
        try:
            label = path.relative_to(ROOT)
        except ValueError:
            label = path
        percent = 100.0 * good / applicable if applicable else 100.0
        print(f"\n{label}  {good}/{applicable} rules clean ({percent:.0f}%)")
        for violation in sorted(checker.violations, key=lambda item: (item.line, item.rule)):
            print(f"  {violation.line:>4}  {violation.rule:<18} {violation.message}")

    percent = 100.0 * passed_rules / total_rules if total_rules else 100.0
    print(
        f"\nFirmware style: {len(files)} files, {passed_rules}/{total_rules} rule checks clean "
        f"({percent:.0f}%), {total_violations} violations"
    )
    print("Rubric: docs/firmware-style-rubric.md. Rules marked review-only there are not checked here.")
    if total_violations and arguments.strict:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
