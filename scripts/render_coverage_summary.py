#!/usr/bin/env python3
"""Render Cobertura totals as a GitHub-compatible HTML summary fragment."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from html import escape
from pathlib import Path
import xml.etree.ElementTree as ET


@dataclass(frozen=True)
class CoverageMetric:
    name: str
    covered: int
    total: int
    rate: float
    threshold: float | None

    @property
    def percent(self) -> float:
        return self.rate * 100.0


def read_metrics(
    report: Path, line_threshold: float, branch_threshold: float
) -> list[CoverageMetric]:
    coverage = ET.parse(report).getroot()
    if coverage.tag != "coverage":
        raise ValueError(f"expected a Cobertura coverage root, found {coverage.tag!r}")

    def metric(
        name: str, covered: str, total: str, rate: str, threshold: float | None
    ) -> CoverageMetric:
        return CoverageMetric(
            name=name,
            covered=int(coverage.attrib[covered]),
            total=int(coverage.attrib[total]),
            rate=float(coverage.attrib[rate]),
            threshold=threshold,
        )

    return [
        metric("Lines", "lines-covered", "lines-valid", "line-rate", line_threshold),
        metric(
            "Functions",
            "functions-covered",
            "functions-valid",
            "function-rate",
            None,
        ),
        metric(
            "Branches",
            "branches-covered",
            "branches-valid",
            "branch-rate",
            branch_threshold,
        ),
    ]


def coverage_indicator(percent: float) -> str:
    if percent >= 90.0:
        return "&#x1F7E2;"
    if percent >= 75.0:
        return "&#x1F7E1;"
    return "&#x1F534;"


def render_html(
    metrics: list[CoverageMetric],
    run_url: str | None = None,
    artifact_name: str = "forgix-application-test-reports",
) -> str:
    rows: list[str] = []
    for metric in metrics:
        if metric.threshold is None:
            required = "&mdash;"
            result = "Reported"
        else:
            required = f"&ge; {metric.threshold:g}%"
            result = (
                "&#x2705; Pass"
                if metric.percent >= metric.threshold
                else "&#x274C; Fail"
            )
        rows.append(
            "<tr>"
            f"<td><strong>{escape(metric.name)}</strong></td>"
            f'<td align="right">{metric.covered} / {metric.total}</td>'
            f'<td align="right">{coverage_indicator(metric.percent)} '
            f"<strong>{metric.percent:.1f}%</strong></td>"
            f'<td align="right">{required}</td>'
            f'<td align="center">{result}</td>'
            "</tr>"
        )

    details = ""
    if run_url:
        details = (
            '<p>Download <a href="{}#artifacts">'
            "<code>{}</code></a> for the detailed "
            "colorized HTML report.</p>"
        ).format(escape(run_url, quote=True), escape(artifact_name))

    return "\n".join(
        [
            "<h2>Application coverage</h2>",
            "<table>",
            "<thead><tr><th>Metric</th><th>Covered</th><th>Coverage</th>"
            "<th>Required</th><th>Gate</th></tr></thead>",
            "<tbody>",
            *rows,
            "</tbody>",
            "</table>",
            "<p>&#x1F7E2; High (&ge; 90%) &nbsp; "
            "&#x1F7E1; Medium (&ge; 75%) &nbsp; "
            "&#x1F534; Low (&lt; 75%)</p>",
            details,
        ]
    ).rstrip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, help="Cobertura XML report")
    parser.add_argument("--line-threshold", type=float, default=100.0)
    parser.add_argument("--branch-threshold", type=float, default=80.0)
    parser.add_argument("--run-url", help="GitHub Actions run URL for the artifact link")
    parser.add_argument(
        "--artifact-name",
        default="forgix-application-test-reports",
        help="artifact label shown in the workflow summary",
    )
    arguments = parser.parse_args()

    metrics = read_metrics(
        arguments.report, arguments.line_threshold, arguments.branch_threshold
    )
    print(
        render_html(metrics, arguments.run_url, arguments.artifact_name),
        end="",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
