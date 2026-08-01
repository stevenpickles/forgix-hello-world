#!/usr/bin/env python3
"""Unit tests for the GitHub coverage summary renderer."""

from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from render_coverage_summary import read_metrics, render_html


COBERTURA = """<?xml version="1.0"?>
<coverage line-rate="1.0" function-rate="1.0" branch-rate="0.803030303"
  lines-covered="63" lines-valid="63" functions-covered="4" functions-valid="4"
  branches-covered="53" branches-valid="66" />
"""


class CoverageSummaryTests(unittest.TestCase):
    def test_renders_line_function_and_branch_totals_with_color(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / "coverage.xml"
            report.write_text(COBERTURA, encoding="utf-8")

            summary = render_html(
                read_metrics(report, line_threshold=100, branch_threshold=80),
                "https://github.com/example/project/actions/runs/123",
                "forgix-application-test-reports-run-123-attempt-2",
            )

        self.assertIn("<table>", summary)
        self.assertIn("<strong>Lines</strong>", summary)
        self.assertIn("63 / 63", summary)
        self.assertIn("&#x1F7E2; <strong>100.0%</strong>", summary)
        self.assertIn("<strong>Branches</strong>", summary)
        self.assertIn("53 / 66", summary)
        self.assertIn("&#x1F7E1; <strong>80.3%</strong>", summary)
        self.assertIn("&ge; 80%", summary)
        self.assertIn("&#x2705; Pass", summary)
        self.assertIn("actions/runs/123#artifacts", summary)
        self.assertIn(
            "forgix-application-test-reports-run-123-attempt-2", summary
        )

    def test_marks_a_branch_gate_failure(self) -> None:
        metrics = read_metrics_from_text(
            COBERTURA.replace('branch-rate="0.803030303"', 'branch-rate="0.5"')
        )
        summary = render_html(metrics)

        self.assertIn("&#x1F534; <strong>50.0%</strong>", summary)
        self.assertIn("&#x274C; Fail", summary)


def read_metrics_from_text(contents: str):
    with tempfile.TemporaryDirectory() as temporary:
        report = Path(temporary) / "coverage.xml"
        report.write_text(contents, encoding="utf-8")
        return read_metrics(report, line_threshold=100, branch_threshold=80)


if __name__ == "__main__":
    unittest.main()
