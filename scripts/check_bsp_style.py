#!/usr/bin/env python3
"""Score BSP sources against the formatting rubric in docs/bsp-style-rubric.md.

Advisory by default: prints a per-file report and exits 0 even when files do not conform, because
the BSP has not been reformatted yet. Pass --strict to make any violation fail, which is how this
should eventually run in CI.

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
DEFAULT_ROOTS = (ROOT / "firmware" / "src" / "bsp", ROOT / "firmware" / "tests" / "support")

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

PUBLIC_FUNCTION = re.compile(r"^(?:MOCK_)?BSP_[A-Z][A-Za-z0-9]*$")
PRIVATE_FUNCTION = re.compile(r"^_[A-Z][A-Za-z0-9_]*$")
PRIVATE_VARIABLE = re.compile(r"^_[a-z][A-Za-z0-9]*$")
ENUM_CONSTANT = re.compile(r"^(?:MOCK_)?BSP_[A-Z0-9_]+$")

# A definition is a line starting in column 0 that closes its parameter list, whose next
# meaningful line is a lone opening brace. Allman makes this reliable without a real parser.
SIGNATURE = re.compile(r"^([A-Za-z_][\w \t*]*?[\w*])\s*\(([^;]*)\)\s*$")
DECLARATION = re.compile(r"^([A-Za-z_][\w \t*]*?[\w*])\s*\(([^;]*)\)\s*;\s*$")
CONTROL = re.compile(r"^\s*(if|while|for|switch)\b")
TYPEDEF_CLOSE = re.compile(r"^\}\s*([A-Za-z_]\w*)\s*;")
TYPEDEF_OPEN = re.compile(r"^typedef\s+(struct|enum)\b\s*(\w+)?\s*$")
ATTRIBUTE = re.compile(r"^\s*(?:__attribute__|__weak\b|__used\b|__STATIC_INLINE\b)")

# Functions here answer to a vendor's prototype, so BSP_PascalCase does not apply to them.
VENDOR_SECTION = "Interrupt Handler Overrides"


@dataclass(frozen=True)
class Violation:
    rule: str
    line: int
    message: str


class Checker:
    """Accumulates violations for one file and remembers which rules were exercised."""

    def __init__(self, path: Path, text: str) -> None:
        self.path = path
        self.text = text
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
        continued = False
        for index, line in enumerate(self.lines):
            stripped = line.strip()
            was_continued, continued = continued, False
            if stripped and not line.startswith((" ", "\t")):
                continued = line.count("(") > line.count(")")
                continue
            if not stripped or index in self.comment_lines or was_continued:
                continued = was_continued and line.count("(") > line.count(")")
                continue
            indent = len(line) - len(line.lstrip(" "))
            if indent % 4 != 0:
                self.fail("C7-indent", index, f"indent of {indent} is not a multiple of four")
            continued = line.count("(") > line.count(")")

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
                if not PRIVATE_FUNCTION.match(name):
                    self.fail("B3-private-fn", index, f"static function '{name}' needs _PascalCase")
            elif self._section_at(index) == VENDOR_SECTION:
                continue  # the vendor owns these names; that is what the section is for
            elif not PUBLIC_FUNCTION.match(name):
                self.fail("B1-public", index, f"public function '{name}' needs BSP_PascalCase")
        for index, line in enumerate(self.lines):
            match = re.match(r"^static\s+(?:const\s+)?[\w ]+?[\w*]\s+(\w+)\s*(?:\[[^\]]*\])?\s*(?:=.*)?;", line)
            if match and not PRIVATE_VARIABLE.match(match.group(1)):
                self.fail("B4-private-var", index, f"file-scope '{match.group(1)}' needs _camelCase")

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
            if kind == "struct" and not name.endswith("_t"):
                self.fail("B5-B6-typedef", close, f"struct typedef '{name}' must end in _t")
            if kind == "enum" and name.endswith("_t"):
                self.fail("B5-B6-typedef", close, f"enum typedef '{name}' must not end in _t")
            if tag != f"{name}_tag":
                self.fail("B5-B6-typedef", index, f"{kind} tag must be '{name}_tag', found '{tag or 'none'}'")
            if kind != "enum":
                continue
            for member in range(index, close):
                constant = re.match(r"^\s{4}(\w+)\s*(?:=|,)", self.lines[member])
                if constant and not ENUM_CONSTANT.match(constant.group(1)):
                    self.fail("B7-enum-constant", member, f"'{constant.group(1)}' needs BSP_SCREAMING_SNAKE")

    def check_documentation(self) -> None:
        if self.is_header:
            self.apply("D5-header-docs")
            for index, line in enumerate(self.lines):
                if line.lstrip().startswith("///"):
                    self.fail("D5-header-docs", index, "documentation belongs on the definition, not here")
            return
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


def discover(roots: list[Path]) -> list[Path]:
    found: list[Path] = []
    for root in roots:
        if not root.exists():
            continue
        for pattern in ("bsp*.h", "bsp*.c", "mock_bsp*.h", "mock_bsp*.c"):
            found.extend(root.rglob(pattern))
    return sorted(set(found))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, action="append", help="directory to scan; repeatable")
    parser.add_argument("--strict", action="store_true", help="exit non-zero when a file violates a rule")
    parser.add_argument("--quiet", action="store_true", help="print only the summary line")
    arguments = parser.parse_args()

    roots = arguments.root or list(DEFAULT_ROOTS)
    files = discover(roots)
    if not files:
        print(f"No BSP sources found under {', '.join(str(root) for root in roots)}", file=sys.stderr)
        return 1

    total_violations = 0
    passed_rules = 0
    total_rules = 0
    for path in files:
        checker = Checker(path, path.read_text(encoding="utf-8"))
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
        f"\nBSP style: {len(files)} files, {passed_rules}/{total_rules} rule checks clean "
        f"({percent:.0f}%), {total_violations} violations"
    )
    print("Rubric: docs/bsp-style-rubric.md. Rules marked review-only there are not checked here.")
    if total_violations and arguments.strict:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
