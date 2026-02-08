"""ConsoleReporter: colored pass/fail output, summary, and failure details.

Generates both console output and a self-contained HTML test report.
"""

from __future__ import annotations

import html
import re
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path

from .core import TestResult, TestVerdict

_ANSI_RE = re.compile(r"\033\[[0-9;]*m")

# ---------------------------------------------------------------------------
# Verdict color / label helpers shared by console + HTML
# ---------------------------------------------------------------------------

_VERDICT_CSS = {
    TestVerdict.PASS: ("#16a34a", "#dcfce7"),     # green
    TestVerdict.FAIL: ("#dc2626", "#fee2e2"),      # red
    TestVerdict.SKIP: ("#ca8a04", "#fef9c3"),      # yellow
    TestVerdict.XFAIL: ("#ca8a04", "#fef9c3"),     # yellow
    TestVerdict.XPASS: ("#dc2626", "#fee2e2"),      # red
    TestVerdict.ERROR: ("#dc2626", "#fee2e2"),      # red
    TestVerdict.TIMEOUT: ("#dc2626", "#fee2e2"),    # red
}

_OK_VERDICTS = {TestVerdict.PASS, TestVerdict.SKIP, TestVerdict.XFAIL}


# ═══════════════════════════════════════════════════════════════════════════
# Console reporter (unchanged public API)
# ═══════════════════════════════════════════════════════════════════════════

class ConsoleReporter:
    def __init__(self, verbose: bool = False, color: bool = True):
        self.verbose = verbose
        self.color = color and sys.stdout.isatty()

    # -- ANSI helpers -------------------------------------------------------

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

    _VERDICT_WIDTH = 7

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

    # -- Console report -----------------------------------------------------

    def report(self, results: list[TestResult]) -> int:
        """Print results and return exit code (0 = success)."""
        by_category: dict[str, list[TestResult]] = defaultdict(list)
        for r in results:
            by_category[r.test.category].append(r)

        max_name = max((len(r.test.metadata.name) for r in results), default=0)

        failures = [
            r for r in results
            if r.verdict in (TestVerdict.FAIL, TestVerdict.ERROR, TestVerdict.XPASS, TestVerdict.TIMEOUT)
        ]
        if failures and not self.verbose:
            print(f"\n{self._bold(self._red('Failures:'))}")
            for r in failures:
                self._print_failure_detail(r)

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
        parts = [self._green(f"{passed} passed")]
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

        for category in sorted(by_category.keys()):
            cat_results = by_category[category]
            print(f"\n{self._bold(f'[{category}]')}")
            for r in cat_results:
                name = r.test.metadata.name
                verdict = self._verdict_str(r.verdict)
                timing = self._dim(f"({r.duration_ms:>4.0f}ms)")
                line = f"  {verdict}  {name:<{max_name}}  {timing}"
                if r.verdict == TestVerdict.XFAIL and r.test.metadata.xfail:
                    line += f"  {self._dim('-- ' + r.test.metadata.xfail)}"
                print(line)
                if self.verbose and r.verdict in (TestVerdict.FAIL, TestVerdict.ERROR, TestVerdict.XPASS):
                    self._print_failure_detail(r)

        return 0 if all(r.verdict in _OK_VERDICTS for r in results) else 1

    def _print_failure_detail(self, r: TestResult) -> None:
        name = r.test.metadata.name
        print(f"\n  {self._red('---')} {self._bold(name)} {self._red('---')}")
        print(f"  File: {self._dim(str(r.test.source_path))}")
        if r.failure_reason:
            print(f"  Reason: {r.failure_reason}")
        if r.compilation and r.compilation.stderr:
            stderr_lines = r.compilation.stderr.strip().split("\n")
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

    # -- File reports -------------------------------------------------------

    def write_report(
        self,
        results: list[TestResult],
        build_dir: Path,
        elapsed_ms: float,
    ) -> Path:
        """Write both an HTML and plain-text test report to *build_dir*.

        Always generates the .txt file so it's available on headless VMs.
        Returns the HTML path as the primary report.
        """
        build_dir.mkdir(parents=True, exist_ok=True)

        # Always write the plain-text report
        txt_path = build_dir / "test_results.txt"
        txt_path.write_text(
            _render_text_report(results, elapsed_ms), encoding="utf-8"
        )

        # Write the HTML report (fall back to txt-only on error)
        html_path = build_dir / "test_results.html"
        try:
            html_content = _render_html_report(results, elapsed_ms)
            html_path.write_text(html_content, encoding="utf-8")
        except Exception:
            return txt_path

        return html_path


# ═══════════════════════════════════════════════════════════════════════════
# HTML report renderer
# ═══════════════════════════════════════════════════════════════════════════

def _esc(text: str) -> str:
    """HTML-escape a string."""
    return html.escape(str(text))


def _badge(verdict: TestVerdict) -> str:
    fg, bg = _VERDICT_CSS.get(verdict, ("#6b7280", "#f3f4f6"))
    return (
        f'<span class="badge" style="color:{fg};background:{bg};">'
        f"{_esc(verdict.value)}</span>"
    )


def _render_html_report(results: list[TestResult], elapsed_ms: float) -> str:
    """Build a full self-contained HTML document."""

    # ── Aggregate data ─────────────────────────────────────────────────
    counts: dict[TestVerdict, int] = defaultdict(int)
    by_category: dict[str, list[TestResult]] = defaultdict(list)
    for r in results:
        counts[r.verdict] += 1
        by_category[r.test.category].append(r)

    total = len(results)
    passed = counts[TestVerdict.PASS]
    failed = counts[TestVerdict.FAIL]
    skipped = counts[TestVerdict.SKIP]
    xfail = counts[TestVerdict.XFAIL]
    xpass = counts[TestVerdict.XPASS]
    errors = counts[TestVerdict.ERROR]
    timeouts = counts[TestVerdict.TIMEOUT]

    all_ok = all(r.verdict in _OK_VERDICTS for r in results)
    pass_rate = (passed / total * 100) if total else 0
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    failures = [
        r for r in results
        if r.verdict not in _OK_VERDICTS
    ]

    # ── Build sections ─────────────────────────────────────────────────

    # Summary cards
    summary_cards = _html_summary_cards(
        total, passed, failed, skipped, xfail, xpass, errors, timeouts,
        elapsed_ms, pass_rate, all_ok,
    )

    # Failure section
    failure_section = ""
    if failures:
        rows = "\n".join(_html_failure_row(r) for r in failures)
        failure_section = f"""
        <section class="section failures-section">
            <h2>⛔  Failures &amp; Errors  <span class="count">({len(failures)})</span></h2>
            <div class="failures-list">{rows}</div>
        </section>"""

    # Category sections
    category_sections = []
    for cat in sorted(by_category.keys()):
        cat_results = by_category[cat]
        cat_pass = sum(1 for r in cat_results if r.verdict == TestVerdict.PASS)
        cat_total = len(cat_results)
        cat_pct = (cat_pass / cat_total * 100) if cat_total else 0

        table_rows = "\n".join(_html_test_row(r) for r in cat_results)
        category_sections.append(f"""
        <section class="section category-section">
            <div class="category-header" onclick="this.parentElement.classList.toggle('collapsed')">
                <h2>
                    <span class="chevron">▾</span>
                    {_esc(cat)}
                    <span class="cat-stats">{cat_pass}/{cat_total} passed · {cat_pct:.0f}%</span>
                </h2>
            </div>
            <table class="test-table">
                <thead>
                    <tr>
                        <th class="col-status">Status</th>
                        <th class="col-name">Test</th>
                        <th class="col-time">Time</th>
                        <th class="col-details">Details</th>
                    </tr>
                </thead>
                <tbody>{table_rows}</tbody>
            </table>
        </section>""")

    categories_html = "\n".join(category_sections)

    # ── Stitch together ────────────────────────────────────────────────
    status_class = "status-pass" if all_ok else "status-fail"
    status_text = "ALL TESTS PASSED" if all_ok else "SOME TESTS FAILED"

    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Cryo Test Results — {now}</title>
<style>
{_CSS}
</style>
</head>
<body>

<header>
    <div class="header-inner">
        <div class="logo-row">
            <h1>❄ Cryo <span class="subtitle">E2E Test Report</span></h1>
            <time>{_esc(now)}</time>
        </div>
        <div class="status-banner {status_class}">{status_text}</div>
    </div>
</header>

<main>
    {summary_cards}
    {failure_section}
    {categories_html}
</main>

<footer>
    <p>Generated by <strong>Cryo Test Runner</strong> · {_esc(now)}</p>
</footer>

<script>
{_JS}
</script>
</body>
</html>
"""


def _html_summary_cards(
    total: int,
    passed: int,
    failed: int,
    skipped: int,
    xfail: int,
    xpass: int,
    errors: int,
    timeouts: int,
    elapsed_ms: float,
    pass_rate: float,
    all_ok: bool,
) -> str:
    """Render the top-level summary cards section."""

    def _card(label: str, value: int | str, css: str = "") -> str:
        cls = f"card {css}" if css else "card"
        return f'<div class="{cls}"><div class="card-value">{value}</div><div class="card-label">{_esc(label)}</div></div>'

    cards = [
        _card("Total", total),
        _card("Passed", passed, "card-green" if passed else ""),
    ]
    if failed:
        cards.append(_card("Failed", failed, "card-red"))
    if errors:
        cards.append(_card("Errors", errors, "card-red"))
    if timeouts:
        cards.append(_card("Timeouts", timeouts, "card-red"))
    if skipped:
        cards.append(_card("Skipped", skipped, "card-yellow"))
    if xfail:
        cards.append(_card("XFail", xfail, "card-yellow"))
    if xpass:
        cards.append(_card("XPass", xpass, "card-red"))

    elapsed_str = f"{elapsed_ms / 1000:.2f}s" if elapsed_ms >= 1000 else f"{elapsed_ms:.0f}ms"
    cards.append(_card("Duration", elapsed_str))
    cards.append(_card("Pass Rate", f"{pass_rate:.1f}%", "card-green" if all_ok else "card-red"))

    # Progress bar
    bar_segments = []
    if total:
        for verdict, count in [
            (TestVerdict.PASS, passed),
            (TestVerdict.XFAIL, xfail),
            (TestVerdict.SKIP, skipped),
            (TestVerdict.FAIL, failed),
            (TestVerdict.ERROR, errors),
            (TestVerdict.TIMEOUT, timeouts),
            (TestVerdict.XPASS, xpass),
        ]:
            if count:
                pct = count / total * 100
                fg, _ = _VERDICT_CSS[verdict]
                bar_segments.append(
                    f'<div class="bar-seg" style="width:{pct:.2f}%;background:{fg};" '
                    f'title="{verdict.value}: {count}"></div>'
                )

    bar_html = "".join(bar_segments)

    return f"""
    <section class="section summary-section">
        <div class="cards">{" ".join(cards)}</div>
        <div class="progress-bar">{bar_html}</div>
    </section>"""


def _html_test_row(r: TestResult) -> str:
    """Render a single table row for a test result."""
    meta = r.test.metadata
    badge = _badge(r.verdict)
    name = _esc(meta.name)
    timing = f"{r.duration_ms:.0f}ms"

    detail_parts: list[str] = []
    if meta.tags:
        tags = " ".join(f'<span class="tag">{_esc(t)}</span>' for t in meta.tags)
        detail_parts.append(tags)
    if r.verdict == TestVerdict.XFAIL and meta.xfail:
        detail_parts.append(f'<span class="xfail-reason">{_esc(meta.xfail)}</span>')
    if r.verdict == TestVerdict.SKIP and meta.skip:
        detail_parts.append(f'<span class="skip-reason">{_esc(meta.skip)}</span>')

    details = " ".join(detail_parts)
    row_cls = "row-fail" if r.verdict not in _OK_VERDICTS else ""

    return f'<tr class="{row_cls}"><td>{badge}</td><td class="test-name">{name}</td><td class="test-time">{timing}</td><td>{details}</td></tr>'


def _html_failure_row(r: TestResult) -> str:
    """Render a detailed expandable failure card."""
    meta = r.test.metadata
    badge = _badge(r.verdict)
    name = _esc(meta.name)

    blocks: list[str] = []

    # Source file
    blocks.append(f'<div class="detail-row"><span class="detail-label">File</span><code>{_esc(str(r.test.source_path))}</code></div>')

    # Failure reason
    if r.failure_reason:
        blocks.append(f'<div class="detail-row"><span class="detail-label">Reason</span><span>{_esc(r.failure_reason)}</span></div>')

    # Compiler stderr
    if r.compilation and r.compilation.stderr:
        stderr = _ANSI_RE.sub("", r.compilation.stderr).strip()
        if stderr:
            blocks.append(f'<div class="detail-row"><span class="detail-label">Compiler Output</span></div><pre class="output-block">{_esc(stderr)}</pre>')

    # Execution stdout
    if r.execution and r.execution.stdout:
        blocks.append(f'<div class="detail-row"><span class="detail-label">Actual Stdout</span></div><pre class="output-block">{_esc(r.execution.stdout.rstrip())}</pre>')

    # Execution stderr
    if r.execution and r.execution.stderr:
        stderr = r.execution.stderr.strip()
        if stderr:
            blocks.append(f'<div class="detail-row"><span class="detail-label">Binary Stderr</span></div><pre class="output-block stderr">{_esc(stderr)}</pre>')

    inner = "\n".join(blocks)

    return f"""
    <details class="failure-card">
        <summary>{badge} <strong>{name}</strong></summary>
        <div class="failure-body">{inner}</div>
    </details>"""


# ═══════════════════════════════════════════════════════════════════════════
# Plain-text fallback (kept for safety)
# ═══════════════════════════════════════════════════════════════════════════

def _render_text_report(results: list[TestResult], elapsed_ms: float) -> str:
    by_category: dict[str, list[TestResult]] = defaultdict(list)
    for r in results:
        by_category[r.test.category].append(r)

    counts: dict[TestVerdict, int] = defaultdict(int)
    for r in results:
        counts[r.verdict] += 1

    lines: list[str] = []
    lines.append("=" * 72)
    lines.append("Cryo E2E Test Results")
    lines.append(f"Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append(f"Total: {len(results)} tests, {elapsed_ms:.0f}ms")
    lines.append(
        f"  PASS: {counts[TestVerdict.PASS]}  FAIL: {counts[TestVerdict.FAIL]}"
        f"  XFAIL: {counts[TestVerdict.XFAIL]}  XPASS: {counts[TestVerdict.XPASS]}"
        f"  SKIP: {counts[TestVerdict.SKIP]}  ERROR: {counts[TestVerdict.ERROR]}"
        f"  TIMEOUT: {counts[TestVerdict.TIMEOUT]}"
    )
    lines.append("=" * 72)

    for category in sorted(by_category.keys()):
        cat_results = by_category[category]
        lines += ["", "", "=" * 72, f"  [{category}]", "=" * 72]

        for r in cat_results:
            name = r.test.metadata.name
            verdict = r.verdict.value
            lines += [
                "", "-" * 72,
                f"  {verdict:<7}  {name}  ({r.duration_ms:.0f}ms)",
                f"  File:    {r.test.source_path}",
            ]
            if r.test.metadata.tags:
                lines.append(f"  Tags:    {', '.join(r.test.metadata.tags)}")
            lines.append("-" * 72)

            if r.verdict in (TestVerdict.PASS, TestVerdict.SKIP):
                continue
            if r.failure_reason:
                lines += [f"  Reason: {r.failure_reason}", ""]
            if r.test.metadata.xfail and r.verdict == TestVerdict.XFAIL:
                lines += [f"  XFAIL:  {r.test.metadata.xfail}", ""]
            if r.compilation and r.compilation.stderr:
                stderr = _ANSI_RE.sub("", r.compilation.stderr).strip()
                if stderr:
                    lines += ["  Compiler output:", ""]
                    lines += [f"    {sl}" for sl in stderr.split("\n")]
                    lines.append("")
            if r.execution and r.execution.stdout:
                lines += ["  Actual stdout:", ""]
                lines += [f"    {sl}" for sl in r.execution.stdout.rstrip("\n").split("\n")]
                lines.append("")
            if r.execution and r.execution.stderr:
                stderr = r.execution.stderr.strip()
                if stderr:
                    lines += ["  Binary stderr:", ""]
                    lines += [f"    {sl}" for sl in stderr.split("\n")]
                    lines.append("")

    lines += ["", "=" * 72]
    return "\n".join(lines) + "\n"


# ═══════════════════════════════════════════════════════════════════════════
# Embedded CSS & JS
# ═══════════════════════════════════════════════════════════════════════════

_CSS = """\
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

:root {
    --bg: #0f172a;
    --surface: #1e293b;
    --surface2: #334155;
    --border: #475569;
    --text: #e2e8f0;
    --text-dim: #94a3b8;
    --green: #22c55e;
    --green-bg: rgba(34,197,94,.12);
    --red: #ef4444;
    --red-bg: rgba(239,68,68,.12);
    --yellow: #eab308;
    --yellow-bg: rgba(234,179,8,.10);
    --accent: #38bdf8;
    --radius: 8px;
    --font: "Inter", -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    --mono: "JetBrains Mono", "Fira Code", "Cascadia Code", "Consolas", monospace;
}

html { font-size: 15px; }
body {
    font-family: var(--font);
    background: var(--bg);
    color: var(--text);
    line-height: 1.6;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
}

/* ── Header ─────────────────────────────────────────────────── */
header {
    background: linear-gradient(135deg, #1e293b 0%, #0f172a 100%);
    border-bottom: 1px solid var(--border);
    padding: 1.5rem 2rem;
}
.header-inner { max-width: 1200px; margin: 0 auto; }
.logo-row {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    flex-wrap: wrap;
    gap: 1rem;
}
.logo-row h1 {
    font-size: 1.5rem;
    font-weight: 700;
    color: var(--accent);
    letter-spacing: -0.02em;
}
.logo-row .subtitle {
    color: var(--text-dim);
    font-weight: 400;
    font-size: 1rem;
    margin-left: 0.5rem;
}
.logo-row time {
    font-family: var(--mono);
    font-size: 0.8rem;
    color: var(--text-dim);
}

.status-banner {
    margin-top: 0.75rem;
    padding: 0.5rem 1rem;
    border-radius: var(--radius);
    font-weight: 700;
    font-size: 0.85rem;
    letter-spacing: 0.05em;
    text-align: center;
}
.status-pass { background: var(--green-bg); color: var(--green); border: 1px solid rgba(34,197,94,.25); }
.status-fail { background: var(--red-bg); color: var(--red); border: 1px solid rgba(239,68,68,.25); }

/* ── Main ───────────────────────────────────────────────────── */
main { max-width: 1200px; margin: 0 auto; padding: 1.5rem 2rem; flex: 1; width: 100%; }

.section { margin-bottom: 2rem; }

/* ── Summary cards ──────────────────────────────────────────── */
.cards {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(120px, 1fr));
    gap: 0.75rem;
    margin-bottom: 1rem;
}
.card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 1rem;
    text-align: center;
}
.card-value {
    font-size: 1.6rem;
    font-weight: 700;
    font-family: var(--mono);
    line-height: 1.2;
}
.card-label {
    font-size: 0.75rem;
    color: var(--text-dim);
    text-transform: uppercase;
    letter-spacing: 0.06em;
    margin-top: 0.25rem;
}
.card-green .card-value { color: var(--green); }
.card-red   .card-value { color: var(--red); }
.card-yellow .card-value { color: var(--yellow); }

.progress-bar {
    display: flex;
    height: 10px;
    border-radius: 999px;
    overflow: hidden;
    background: var(--surface2);
}
.bar-seg { transition: width .3s ease; }

/* ── Failure section ────────────────────────────────────────── */
.failures-section h2 { color: var(--red); margin-bottom: 0.75rem; font-size: 1.15rem; }
.failures-section .count { font-weight: 400; color: var(--text-dim); font-size: 0.9rem; }

.failure-card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-left: 3px solid var(--red);
    border-radius: var(--radius);
    margin-bottom: 0.5rem;
    overflow: hidden;
}
.failure-card summary {
    padding: 0.75rem 1rem;
    cursor: pointer;
    display: flex;
    align-items: center;
    gap: 0.5rem;
    user-select: none;
    font-size: 0.9rem;
}
.failure-card summary::-webkit-details-marker { display: none; }
.failure-card summary::before {
    content: "▸";
    font-size: 0.75rem;
    color: var(--text-dim);
    transition: transform .15s;
}
.failure-card[open] summary::before { transform: rotate(90deg); }
.failure-body {
    padding: 0 1rem 1rem 1rem;
    border-top: 1px solid var(--border);
}
.detail-row {
    display: flex;
    gap: 0.75rem;
    padding: 0.4rem 0;
    font-size: 0.85rem;
    align-items: baseline;
    flex-wrap: wrap;
}
.detail-label {
    font-weight: 600;
    color: var(--text-dim);
    text-transform: uppercase;
    font-size: 0.7rem;
    letter-spacing: 0.05em;
    min-width: 110px;
    flex-shrink: 0;
}
.detail-row code {
    font-family: var(--mono);
    font-size: 0.8rem;
    color: var(--accent);
    word-break: break-all;
}

.output-block {
    background: #0b1120;
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 0.75rem 1rem;
    font-family: var(--mono);
    font-size: 0.78rem;
    line-height: 1.5;
    overflow-x: auto;
    margin: 0.4rem 0 0.6rem 0;
    color: var(--text);
    white-space: pre-wrap;
    word-break: break-word;
    max-height: 300px;
    overflow-y: auto;
}
.output-block.stderr { border-left: 3px solid var(--red); }

/* ── Category sections ──────────────────────────────────────── */
.category-header {
    cursor: pointer;
    user-select: none;
}
.category-header h2 {
    font-size: 1.1rem;
    display: flex;
    align-items: center;
    gap: 0.5rem;
    color: var(--accent);
    padding: 0.5rem 0;
}
.category-header .chevron {
    font-size: 0.8rem;
    transition: transform .15s;
    display: inline-block;
}
.collapsed .category-header .chevron { transform: rotate(-90deg); }
.collapsed .test-table { display: none; }
.cat-stats {
    font-weight: 400;
    font-size: 0.8rem;
    color: var(--text-dim);
    margin-left: auto;
}

/* ── Test table ─────────────────────────────────────────────── */
.test-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.85rem;
}
.test-table th {
    text-align: left;
    font-weight: 600;
    color: var(--text-dim);
    font-size: 0.7rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    padding: 0.5rem 0.75rem;
    border-bottom: 1px solid var(--border);
}
.test-table td {
    padding: 0.5rem 0.75rem;
    border-bottom: 1px solid rgba(71,85,105,.3);
    vertical-align: middle;
}
.test-table tbody tr:hover { background: rgba(56,189,248,.04); }
.row-fail { background: rgba(239,68,68,.05); }
.row-fail:hover { background: rgba(239,68,68,.08) !important; }

.col-status { width: 80px; }
.col-time { width: 80px; text-align: right; }
.col-name { font-family: var(--mono); font-size: 0.82rem; }
.test-time { font-family: var(--mono); color: var(--text-dim); font-size: 0.8rem; text-align: right; }

/* ── Badge ──────────────────────────────────────────────────── */
.badge {
    display: inline-block;
    padding: 0.15rem 0.5rem;
    border-radius: 4px;
    font-weight: 700;
    font-size: 0.7rem;
    font-family: var(--mono);
    letter-spacing: 0.04em;
    text-transform: uppercase;
}

/* ── Tags ───────────────────────────────────────────────────── */
.tag {
    display: inline-block;
    padding: 0.1rem 0.4rem;
    border-radius: 4px;
    font-size: 0.7rem;
    font-family: var(--mono);
    background: var(--surface2);
    color: var(--text-dim);
    margin-right: 0.25rem;
}
.xfail-reason, .skip-reason {
    font-size: 0.8rem;
    color: var(--text-dim);
    font-style: italic;
}

/* ── Footer ─────────────────────────────────────────────────── */
footer {
    text-align: center;
    padding: 1.5rem 2rem;
    font-size: 0.75rem;
    color: var(--text-dim);
    border-top: 1px solid var(--border);
}

/* ── Responsive ─────────────────────────────────────────────── */
@media (max-width: 640px) {
    main, header { padding: 1rem; }
    .cards { grid-template-columns: repeat(auto-fill, minmax(90px, 1fr)); }
    .test-table { font-size: 0.78rem; }
}

/* ── Print ──────────────────────────────────────────────────── */
@media print {
    body { background: #fff; color: #111; }
    header { background: #fff; border-color: #ddd; }
    .logo-row h1 { color: #0284c7; }
    .card { border-color: #ddd; background: #f8fafc; }
    .failure-card { border-color: #ddd; background: #fff; }
    .output-block { background: #f1f5f9; border-color: #ddd; }
    .test-table td { border-color: #e2e8f0; }
    footer { border-color: #ddd; }
}
"""

_JS = """\
// Auto-expand first failure for immediate visibility
document.addEventListener("DOMContentLoaded", function() {
    var first = document.querySelector(".failure-card");
    if (first) first.open = true;
});
"""