#!/usr/bin/env python3
"""Unit tests for check_firmware_layers.py's internal-header ownership rule.

Run as `python scripts/test_check_firmware_layers.py` from the repository
root, matching the other script tests. The ownership rule is pure filename
logic, so each case builds a miniature tree in a temporary directory; the
last test runs the whole checker against the real repository, which is the
integration gate CI relies on.
"""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from check_firmware_layers import check_internal_header_ownership, main


def write(root: Path, relative: str, text: str) -> Path:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


class InternalHeaderOwnership(unittest.TestCase):
    def run_check(self, files: list[Path], root: Path) -> int:
        return check_internal_header_ownership(files, root)

    def test_the_owning_source_may_include_its_internal_header(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            files = [
                write(root, "src/application/application_console_internal.h", ""),
                write(root, "src/application/application_console.c",
                      '#include "application_console_internal.h"\n'),
            ]
            self.assertEqual(self.run_check(files, root), 1)

    def test_a_split_source_of_the_owning_module_may_include_it(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            files = [
                write(root, "src/application/application_console_internal.h", ""),
                write(root, "src/application/application_console_status.c",
                      '#include "application_console_internal.h"\n'),
            ]
            self.assertEqual(self.run_check(files, root), 1)

    def test_an_unrelated_source_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            files = [
                write(root, "src/application/application_ui.c",
                      '#include "application_console_internal.h"\n'),
            ]
            with self.assertRaises(ValueError) as caught:
                self.run_check(files, root)
            self.assertIn("application_console_internal.h", str(caught.exception))

    def test_a_prefix_that_is_not_a_module_boundary_is_rejected(self) -> None:
        # application_consolex.c shares the spelling but not the module: the
        # owner match requires exactly <module> or <module>_ at the boundary.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            files = [
                write(root, "src/application/application_consolex.c",
                      '#include "application_console_internal.h"\n'),
            ]
            with self.assertRaises(ValueError):
                self.run_check(files, root)

    def test_a_public_header_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            files = [
                write(root, "src/application/application_console.h",
                      '#include "application_console_internal.h"\n'),
            ]
            with self.assertRaises(ValueError):
                self.run_check(files, root)

    def test_a_test_file_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            files = [
                write(root, "tests/test_application_console.c",
                      '#include "application_console_internal.h"\n'),
            ]
            with self.assertRaises(ValueError):
                self.run_check(files, root)

    def test_prose_mentioning_an_internal_header_is_not_an_inclusion(self) -> None:
        # application_ibit_steps_board.h names its module's internal header in
        # a comment; only #include directives are subject to the rule.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            files = [
                write(root, "src/application/application_ibit_steps_board.h",
                      "/* state lives in application_ibit_internal.h */\n"),
            ]
            self.assertEqual(self.run_check(files, root), 0)


class RepositoryConsistency(unittest.TestCase):
    def test_repository_is_currently_consistent(self) -> None:
        self.assertEqual(main(), 0)


if __name__ == "__main__":
    unittest.main()
