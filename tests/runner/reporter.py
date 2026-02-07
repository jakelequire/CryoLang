"""ConsoleReporter: colored pass/fail output, summary, and failure details."""

from __future__ import annotations

import re
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path

from .core import TestResult, TestVerdict

_ANSI_RE = re.compile(r"\033\[[0-9;]*m")


class ConsoleReporter:
    def __init__(self, verbose: bool = False, color: bool = True):
        self.verbose = verbose
        self.color = color and sys.stdout.isatty()

    def _c(self, code: str, text: str) -> str:
        if not self.color:
            return text
        return f"\033[{code}m{text}\033[0m"

    def _green(self, t: str) -> str:
        return self._c("32", t)

    def _red(self, t: str) -> str:
        return self._c("31", t)

    def _yellow(self, t: str) -> str:
        return self._c("33", t)

    def _cyan(self, t: str) -> str:
        return self._c("36", t)

    def _bold(self, t: str) -> str:
        return self._c("1", t)

    def _dim(self, t: str) -> str:
        return self._c("2", t)

    _VERDICT_WIDTH = 7  # "TIMEOUT" is the longest at 7 chars

    def _verdict_str(self, v: TestVerdict, pad: bool = True) -> str:
        label = v.value
        mapping = {
            TestVerdict.PASS: self._green,
            TestVerdict.FAIL: self._red,
            TestVerdict.SKIP: self._yellow,
            TestVerdict.XFAIL: self._yellow,
            TestVerdict.XPASS: self._red,
            TestVerdict.ERROR: self._red,
            TestVerdict.TIMEOUT: self._red,
        }
        colorize = mapping.get(v, lambda t: t)
        if pad:
            return colorize(f"{label:<{self._VERDICT_WIDTH}}")
        return colorize(label)

    def report(self, results: list[TestResult]) -> int:
        """Print results and return exit code (0 = success)."""
        # Group by category
        by_category: dict[str, list[TestResult]] = defaultdict(list)
        for r in results:
            by_category[r.test.category].append(r)

        # Find the longest test name per category for alignment
        max_name = max((len(r.test.metadata.name) for r in results), default=0)

        # Print results per category
        for category in sorted(by_category.keys()):
            cat_results = by_category[category]
            print(f"\n{self._bold(f'[{category}]')}")
            for r in cat_results:
                name = r.test.metadata.name
                verdict = self._verdict_str(r.verdict)
                timing = self._dim(f"({r.duration_ms:>4.0f}ms)")
                print(f"  {verdict}  {name:<{max_name}}  {timing}")

                if self.verbose and r.verdict in (TestVerdict.FAIL, TestVerdict.ERROR, TestVerdict.XPASS):
                    self._print_failure_detail(r)

        # Summary
        counts = defaultdict(int)
        for r in results:
            counts[r.verdict] += 1

        total = len(results)
        passed = counts[TestVerdict.PASS]
        failed = counts[TestVerdict.FAIL]
        skipped = counts[TestVerdict.SKIP]
        xfail = counts[TestVerdict.XFAIL]
        xpass = counts[TestVerdict.XPASS]
        errors = counts[TestVerdict.ERROR]
        timeouts = counts[TestVerdict.TIMEOUT]

        print(f"\n{self._bold('Summary:')}")
        parts = []
        parts.append(self._green(f"{passed} passed"))
        if failed:
            parts.append(self._red(f"{failed} failed"))
        if skipped:
            parts.append(self._yellow(f"{skipped} skipped"))
        if xfail:
            parts.append(self._yellow(f"{xfail} xfail"))
        if xpass:
            parts.append(self._red(f"{xpass} xpass"))
        if errors:
            parts.append(self._red(f"{errors} errors"))
        if timeouts:
            parts.append(self._red(f"{timeouts} timeouts"))
        parts.append(f"{total} total")

        print(f"  {', '.join(parts)}")

        # Print failure details (non-verbose mode)
        failures = [r for r in results if r.verdict in (TestVerdict.FAIL, TestVerdict.ERROR, TestVerdict.XPASS, TestVerdict.TIMEOUT)]
        if failures and not self.verbose:
            print(f"\n{self._bold(self._red('Failures:'))}")
            for r in failures:
                self._print_failure_detail(r)

        # Exit code: 0 if only PASS, SKIP, XFAIL
        ok_verdicts = {TestVerdict.PASS, TestVerdict.SKIP, TestVerdict.XFAIL}
        return 0 if all(r.verdict in ok_verdicts for r in results) else 1

    def _print_failure_detail(self, r: TestResult) -> None:
        name = r.test.metadata.name
        print(f"\n  {self._red('---')} {self._bold(name)} {self._red('---')}")
        print(f"  File: {self._dim(str(r.test.source_path))}")
        if r.failure_reason:
            print(f"  Reason: {r.failure_reason}")
        if r.compilation and r.compilation.stderr:
            stderr_lines = r.compilation.stderr.strip().split("\n")
            # Show at most 20 lines of compiler stderr
            for line in stderr_lines[:20]:
                print(f"    {self._dim(line)}")
            if len(stderr_lines) > 20:
                print(f"    {self._dim(f'... ({len(stderr_lines) - 20} more lines)')}")
        if r.execution and r.execution.stdout:
            print(f"  Actual stdout:")
            for line in r.execution.stdout.split("\n"):
                print(f"    {self._dim(line)}")
        if r.execution and r.execution.stderr:
            print(f"  Binary stderr:")
            for line in r.execution.stderr.strip().split("\n")[:10]:
                print(f"    {self._dim(line)}")

    def write_report(self, results: list[TestResult], build_dir: Path, elapsed_ms: float) -> Path:
        """Write a verbose plain-text results file to the build dir."""
        build_dir.mkdir(parents=True, exist_ok=True)
        report_path = build_dir / "test_results.txt"

        by_category: dict[str, list[TestResult]] = defaultdict(list)
        for r in results:
            by_category[r.test.category].append(r)

        counts = defaultdict(int)
        for r in results:
            counts[r.verdict] += 1

        lines: list[str] = []
        lines.append("=" * 72)
        lines.append("Cryo E2E Test Results")
        lines.append(f"Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        lines.append(f"Total: {len(results)} tests, {elapsed_ms:.0f}ms")
        lines.append(f"  PASS: {counts[TestVerdict.PASS]}  FAIL: {counts[TestVerdict.FAIL]}"
                      f"  XFAIL: {counts[TestVerdict.XFAIL]}  XPASS: {counts[TestVerdict.XPASS]}"
                      f"  SKIP: {counts[TestVerdict.SKIP]}  ERROR: {counts[TestVerdict.ERROR]}"
                      f"  TIMEOUT: {counts[TestVerdict.TIMEOUT]}")
        lines.append("=" * 72)

        for category in sorted(by_category.keys()):
            cat_results = by_category[category]
            lines.append("")
            lines.append("")
            lines.append("=" * 72)
            lines.append(f"  [{category}]")
            lines.append("=" * 72)

            for r in cat_results:
                name = r.test.metadata.name
                verdict = r.verdict.value
                source = r.test.source_path

                lines.append("")
                lines.append("-" * 72)
                lines.append(f"  {verdict:<7}  {name}  ({r.duration_ms:.0f}ms)")
                lines.append(f"  File:    {source}")
                if r.test.metadata.tags:
                    lines.append(f"  Tags:    {', '.join(r.test.metadata.tags)}")
                lines.append("-" * 72)

                # Show details for anything that's not a clean PASS or SKIP
                if r.verdict in (TestVerdict.PASS, TestVerdict.SKIP):
                    continue

                if r.failure_reason:
                    lines.append(f"  Reason: {r.failure_reason}")
                    lines.append("")

                if r.test.metadata.xfail and r.verdict == TestVerdict.XFAIL:
                    lines.append(f"  XFAIL:  {r.test.metadata.xfail}")
                    lines.append("")

                if r.compilation and r.compilation.stderr:
                    stderr = _ANSI_RE.sub("", r.compilation.stderr).strip()
                    if stderr:
                        lines.append("  Compiler output:")
                        lines.append("")
                        for sl in stderr.split("\n"):
                            lines.append(f"    {sl}")
                        lines.append("")

                if r.execution and r.execution.stdout:
                    lines.append("  Actual stdout:")
                    lines.append("")
                    for sl in r.execution.stdout.rstrip("\n").split("\n"):
                        lines.append(f"    {sl}")
                    lines.append("")

                if r.execution and r.execution.stderr:
                    stderr = r.execution.stderr.strip()
                    if stderr:
                        lines.append("  Binary stderr:")
                        lines.append("")
                        for sl in stderr.split("\n"):
                            lines.append(f"    {sl}")
                        lines.append("")

        lines.append("")
        lines.append("=" * 72)

        report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return report_path
