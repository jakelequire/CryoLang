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


def _read_source_safe(path: Path) -> str:
    """Safely read a test source file, returning empty string on error."""
    try:
        return path.read_text(encoding="utf-8")
    except Exception:
        return ""


# ── Syntax highlighting ───────────────────────────────────────────────────

_HIGHLIGHT_RE = re.compile("|".join([
    r"(?P<comment>//[^\n]*)",
    r'(?P<string>"(?:[^"\\]|\\.)*")',
    r"(?P<keyword>\b(?:function|return|if|else|while|for|const|mut|type|"
    r"struct|enum|implement|namespace|match|break|continue|loop|import|"
    r"public|private|static|class|new|this|true|false|extern)\b)",
    r"(?P<type>\b(?:int|i8|i16|i32|i64|u8|u16|u32|u64|float|f32|f64|"
    r"boolean|char|string|void|String)\b)",
    r"(?P<number>\b\d+\.?\d*\b)",
    r"(?P<arrow>->)",
    r"(?P<decorator>@\w+)",
]))


def _highlight_cryo(source: str) -> str:
    """Syntax-highlight Cryo source code, returning safe HTML."""
    result: list[str] = []
    last = 0
    for m in _HIGHLIGHT_RE.finditer(source):
        if m.start() > last:
            result.append(html.escape(source[last:m.start()]))
        for kind in ("comment", "string", "keyword", "type", "number", "arrow", "decorator"):
            if m.group(kind) is not None:
                result.append(f'<span class="hl-{kind}">{html.escape(m.group())}</span>')
                break
        last = m.end()
    if last < len(source):
        result.append(html.escape(source[last:]))
    return "".join(result)


def _source_block(source: str) -> str:
    """Render highlighted Cryo source with line numbers."""
    if not source.strip():
        return '<pre class="source-block"><code><span class="hl-comment">(source not available)</span></code></pre>'
    highlighted = _highlight_cryo(source)
    lines = highlighted.split("\n")
    while lines and not lines[-1].strip():
        lines.pop()
    width = len(str(len(lines)))
    numbered = []
    for i, line in enumerate(lines, 1):
        ln = str(i).rjust(width)
        numbered.append(f'<span class="line"><span class="ln">{ln}</span>{line}</span>')
    return f'<pre class="source-block"><code>{"".join(numbered)}</code></pre>'


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

    failures = [r for r in results if r.verdict not in _OK_VERDICTS]

    # ── Read all source files once ─────────────────────────────────────
    sources: dict[str, str] = {}
    for r in results:
        key = str(r.test.source_path)
        if key not in sources:
            sources[key] = _read_source_safe(r.test.source_path)

    # ── Build sections ─────────────────────────────────────────────────
    summary_cards = _html_summary_cards(
        total, passed, failed, skipped, xfail, xpass, errors, timeouts,
        elapsed_ms, pass_rate, all_ok,
    )

    performance_section = _html_performance_section(results)

    failure_section = ""
    if failures:
        rows = "\n".join(
            _html_failure_row(r, sources.get(str(r.test.source_path), ""))
            for r in failures
        )
        failure_section = f"""
        <section class="section failures-section">
            <h2>Failures &amp; Errors  <span class="count">({len(failures)})</span></h2>
            <div class="failures-list">{rows}</div>
        </section>"""

    # Category sections
    category_sections = []
    for cat in sorted(by_category.keys()):
        cat_results = by_category[cat]
        cat_pass = sum(1 for r in cat_results if r.verdict == TestVerdict.PASS)
        cat_total = len(cat_results)
        cat_pct = (cat_pass / cat_total * 100) if cat_total else 0

        test_cards = "\n".join(
            _html_test_card(r, sources.get(str(r.test.source_path), ""))
            for r in cat_results
        )
        category_sections.append(f"""
        <section class="section category-section">
            <div class="category-header" onclick="this.parentElement.classList.toggle('collapsed')">
                <h2>
                    <span class="chevron">▾</span>
                    {_esc(cat)}
                    <span class="cat-stats">{cat_pass}/{cat_total} passed · {cat_pct:.0f}%</span>
                </h2>
            </div>
            <div class="test-list">{test_cards}</div>
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
            <h1>&#10052; Cryo <span class="subtitle">E2E Test Report</span></h1>
            <time>{_esc(now)}</time>
        </div>
        <div class="status-banner {status_class}">{status_text}</div>
    </div>
</header>

<div class="toolbar">
    <div class="toolbar-inner">
        <input type="text" id="search-input" class="search-box"
               placeholder="Search tests by name..." oninput="filterTests(this.value)">
        <div class="filter-chips">
            <button class="chip active" onclick="filterByVerdict('all', this)">All</button>
            <button class="chip chip-pass" onclick="filterByVerdict('pass', this)">Pass</button>
            <button class="chip chip-fail" onclick="filterByVerdict('fail', this)">Fail</button>
            <button class="chip chip-skip" onclick="filterByVerdict('skip', this)">Skip</button>
        </div>
    </div>
</div>

<main>
    {summary_cards}
    {performance_section}
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


def _html_performance_section(results: list[TestResult]) -> str:
    """Render a performance insights section with timing breakdowns."""
    timed = [(r, r.duration_ms) for r in results if r.duration_ms > 0]
    if not timed:
        return ""

    timed.sort(key=lambda x: x[1], reverse=True)
    slowest = timed[:5]

    avg_time = sum(t for _, t in timed) / len(timed)

    compile_times = [
        r.compilation.duration_ms for r in results
        if r.compilation and r.compilation.duration_ms > 0
    ]
    exec_times = [
        r.execution.duration_ms for r in results
        if r.execution and r.execution.duration_ms > 0
    ]
    avg_compile = sum(compile_times) / len(compile_times) if compile_times else 0
    avg_exec = sum(exec_times) / len(exec_times) if exec_times else 0

    slowest_rows = ""
    for r, t in slowest:
        name = _esc(r.test.metadata.name)
        badge = _badge(r.verdict)
        slowest_rows += (
            f'<tr><td>{badge}</td>'
            f'<td class="test-name">{name}</td>'
            f'<td class="test-time">{t:.0f}ms</td></tr>\n'
        )

    return f"""
    <section class="section performance-section">
        <div class="category-header" onclick="this.parentElement.classList.toggle('collapsed')">
            <h2>
                <span class="chevron">▾</span>
                Performance Insights
            </h2>
        </div>
        <div class="perf-grid">
            <div class="perf-card">
                <div class="perf-value">{avg_time:.0f}ms</div>
                <div class="perf-label">Avg Total Time</div>
            </div>
            <div class="perf-card">
                <div class="perf-value">{avg_compile:.0f}ms</div>
                <div class="perf-label">Avg Compile Time</div>
            </div>
            <div class="perf-card">
                <div class="perf-value">{avg_exec:.0f}ms</div>
                <div class="perf-label">Avg Exec Time</div>
            </div>
        </div>
        <h3 class="perf-subtitle">Slowest Tests</h3>
        <table class="test-table slowest-table">
            <tbody>{slowest_rows}</tbody>
        </table>
    </section>"""


def _html_test_card(r: TestResult, source: str) -> str:
    """Render an expandable test card for category sections."""
    meta = r.test.metadata
    badge = _badge(r.verdict)
    name = _esc(meta.name)
    timing = f"{r.duration_ms:.0f}ms"

    mode_str = _esc(meta.mode.value)
    expect_str = _esc(meta.expect.value) if meta.expect else ""

    tags_html = ""
    if meta.tags:
        tags = " ".join(f'<span class="tag">{_esc(t)}</span>' for t in meta.tags)
        tags_html = f'<span class="tags-group">{tags}</span>'

    inline_note = ""
    if r.verdict == TestVerdict.XFAIL and meta.xfail:
        inline_note = f'<span class="xfail-reason">{_esc(meta.xfail)}</span>'
    elif r.verdict == TestVerdict.SKIP and meta.skip:
        inline_note = f'<span class="skip-reason">{_esc(meta.skip)}</span>'

    # ── Expanded body ──────────────────────────────────────────────
    body_parts: list[str] = []

    if meta.description:
        body_parts.append(f'<p class="test-description">{_esc(meta.description)}</p>')

    # Metadata chips
    meta_bits = [f'<span class="meta-chip">Mode: {mode_str}</span>']
    if expect_str:
        meta_bits.append(f'<span class="meta-chip">Expect: {expect_str}</span>')
    if meta.timeout != 10:
        meta_bits.append(f'<span class="meta-chip">Timeout: {meta.timeout}s</span>')
    if r.compilation:
        meta_bits.append(f'<span class="meta-chip">Compile: {r.compilation.duration_ms:.0f}ms</span>')
    if r.execution:
        meta_bits.append(f'<span class="meta-chip">Exec: {r.execution.duration_ms:.0f}ms</span>')
    body_parts.append(f'<div class="meta-chips">{"".join(meta_bits)}</div>')

    # Expected output
    if meta.stdout_lines:
        expected = _esc("\n".join(meta.stdout_lines))
        body_parts.append(
            f'<div class="expected-section">'
            f'<h4>Expected Output</h4>'
            f'<pre class="output-block compact">{expected}</pre>'
            f'</div>'
        )

    # Actual output
    if r.execution and r.execution.stdout:
        actual = _esc(r.execution.stdout.rstrip())
        body_parts.append(
            f'<div class="actual-section">'
            f'<h4>Actual Output</h4>'
            f'<pre class="output-block compact">{actual}</pre>'
            f'</div>'
        )

    # Source code viewer
    source_filename = r.test.source_path.name
    body_parts.append(
        f'<details class="source-viewer">'
        f'<summary>Source Code <span class="source-path">({_esc(source_filename)})</span></summary>'
        f'{_source_block(source)}'
        f'</details>'
    )

    # Failure details inline
    if r.verdict not in _OK_VERDICTS:
        if r.failure_reason:
            body_parts.append(
                f'<div class="detail-row">'
                f'<span class="detail-label">Failure</span>'
                f'<span class="failure-reason-text">{_esc(r.failure_reason)}</span>'
                f'</div>'
            )
        if r.compilation and r.compilation.stderr:
            stderr = _ANSI_RE.sub("", r.compilation.stderr).strip()
            if stderr:
                body_parts.append(f'<pre class="output-block stderr">{_esc(stderr)}</pre>')

    body_html = "\n".join(body_parts)
    row_cls = "test-card-fail" if r.verdict not in _OK_VERDICTS else ""
    verdict_name = r.verdict.value.lower()

    return f"""
    <details class="test-card {row_cls}" data-test-name="{_esc(meta.name.lower())}" data-verdict="{verdict_name}">
        <summary class="test-card-summary">
            {badge}
            <span class="test-name">{name}</span>
            {tags_html}
            {inline_note}
            <span class="test-time">{timing}</span>
        </summary>
        <div class="test-card-body">{body_html}</div>
    </details>"""


def _html_failure_row(r: TestResult, source: str) -> str:
    """Render a detailed expandable failure card with source code."""
    meta = r.test.metadata
    badge = _badge(r.verdict)
    name = _esc(meta.name)

    blocks: list[str] = []

    # Source file
    blocks.append(
        f'<div class="detail-row">'
        f'<span class="detail-label">File</span>'
        f'<code>{_esc(str(r.test.source_path))}</code>'
        f'</div>'
    )

    # Failure reason
    if r.failure_reason:
        blocks.append(
            f'<div class="detail-row">'
            f'<span class="detail-label">Reason</span>'
            f'<span class="failure-reason-text">{_esc(r.failure_reason)}</span>'
            f'</div>'
        )

    # Test metadata grid
    meta_items = []
    if meta.expect:
        meta_items.append(
            f'<div class="meta-item"><span class="meta-key">Expected</span>'
            f'<span class="meta-val">{_esc(meta.expect.value)}</span></div>'
        )
    meta_items.append(
        f'<div class="meta-item"><span class="meta-key">Mode</span>'
        f'<span class="meta-val">{_esc(meta.mode.value)}</span></div>'
    )
    if r.compilation:
        meta_items.append(
            f'<div class="meta-item"><span class="meta-key">Compile Time</span>'
            f'<span class="meta-val">{r.compilation.duration_ms:.0f}ms</span></div>'
        )
    if r.execution:
        meta_items.append(
            f'<div class="meta-item"><span class="meta-key">Exec Time</span>'
            f'<span class="meta-val">{r.execution.duration_ms:.0f}ms</span></div>'
        )
    meta_items.append(
        f'<div class="meta-item"><span class="meta-key">Total</span>'
        f'<span class="meta-val">{r.duration_ms:.0f}ms</span></div>'
    )
    if meta.compiler_args:
        meta_items.append(
            f'<div class="meta-item"><span class="meta-key">Compiler Args</span>'
            f'<span class="meta-val">{_esc(meta.compiler_args)}</span></div>'
        )
    blocks.append(f'<div class="meta-grid">{"".join(meta_items)}</div>')

    # Source code viewer
    source_filename = r.test.source_path.name
    blocks.append(
        f'<details class="source-viewer">'
        f'<summary>View Source Code '
        f'<span class="source-path">({_esc(source_filename)})</span></summary>'
        f'{_source_block(source)}'
        f'</details>'
    )

    # Expected vs Actual comparison
    if meta.stdout_lines and r.execution and r.execution.stdout:
        expected = "\n".join(meta.stdout_lines)
        actual = r.execution.stdout.rstrip()
        blocks.append(
            f'<div class="comparison">'
            f'<div class="comparison-col">'
            f'<h4>Expected Output</h4>'
            f'<pre class="output-block">{_esc(expected)}</pre>'
            f'</div>'
            f'<div class="comparison-col">'
            f'<h4>Actual Output</h4>'
            f'<pre class="output-block">{_esc(actual)}</pre>'
            f'</div>'
            f'</div>'
        )
    elif r.execution and r.execution.stdout:
        blocks.append(
            f'<div class="detail-row"><span class="detail-label">Actual Stdout</span></div>'
            f'<pre class="output-block">{_esc(r.execution.stdout.rstrip())}</pre>'
        )

    # Compiler stderr
    if r.compilation and r.compilation.stderr:
        stderr = _ANSI_RE.sub("", r.compilation.stderr).strip()
        if stderr:
            blocks.append(
                f'<div class="detail-row"><span class="detail-label">Compiler Output</span></div>'
                f'<pre class="output-block stderr">{_esc(stderr)}</pre>'
            )

    # Binary stderr
    if r.execution and r.execution.stderr:
        stderr = r.execution.stderr.strip()
        if stderr:
            blocks.append(
                f'<div class="detail-row"><span class="detail-label">Binary Stderr</span></div>'
                f'<pre class="output-block stderr">{_esc(stderr)}</pre>'
            )

    inner = "\n".join(blocks)
    timing_html = f'<span class="failure-time">{r.duration_ms:.0f}ms</span>'

    return f"""
    <details class="failure-card" data-test-name="{_esc(meta.name.lower())}">
        <summary>{badge} <strong>{name}</strong>{timing_html}</summary>
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
    --surface3: #0f1729;
    --border: #475569;
    --border-dim: rgba(71,85,105,.3);
    --text: #e2e8f0;
    --text-dim: #94a3b8;
    --green: #22c55e;
    --green-bg: rgba(34,197,94,.12);
    --red: #ef4444;
    --red-bg: rgba(239,68,68,.12);
    --yellow: #eab308;
    --yellow-bg: rgba(234,179,8,.10);
    --accent: #38bdf8;
    --accent-dim: rgba(56,189,248,.15);
    --radius: 8px;
    --radius-sm: 6px;
    --font: "Inter", -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    --mono: "JetBrains Mono", "Fira Code", "Cascadia Code", "Consolas", monospace;
    --shadow: 0 1px 3px rgba(0,0,0,.3), 0 1px 2px rgba(0,0,0,.2);
    --shadow-lg: 0 4px 6px rgba(0,0,0,.3), 0 2px 4px rgba(0,0,0,.2);
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
    display: flex; align-items: baseline;
    justify-content: space-between; flex-wrap: wrap; gap: 1rem;
}
.logo-row h1 {
    font-size: 1.5rem; font-weight: 700; color: var(--accent);
    letter-spacing: -0.02em;
}
.logo-row .subtitle {
    color: var(--text-dim); font-weight: 400;
    font-size: 1rem; margin-left: 0.5rem;
}
.logo-row time {
    font-family: var(--mono); font-size: 0.8rem; color: var(--text-dim);
}

.status-banner {
    margin-top: 0.75rem; padding: 0.5rem 1rem;
    border-radius: var(--radius); font-weight: 700;
    font-size: 0.85rem; letter-spacing: 0.05em; text-align: center;
}
.status-pass { background: var(--green-bg); color: var(--green); border: 1px solid rgba(34,197,94,.25); }
.status-fail { background: var(--red-bg); color: var(--red); border: 1px solid rgba(239,68,68,.25); }

/* ── Toolbar ────────────────────────────────────────────────── */
.toolbar {
    background: var(--surface); border-bottom: 1px solid var(--border);
    padding: 0.75rem 2rem; position: sticky; top: 0; z-index: 100;
}
.toolbar-inner {
    max-width: 1200px; margin: 0 auto;
    display: flex; align-items: center; gap: 1rem; flex-wrap: wrap;
}
.search-box {
    flex: 1; min-width: 200px;
    background: var(--bg); border: 1px solid var(--border);
    border-radius: var(--radius); padding: 0.5rem 0.75rem;
    color: var(--text); font-family: var(--font); font-size: 0.85rem;
    outline: none; transition: border-color .15s;
}
.search-box:focus { border-color: var(--accent); }
.search-box::placeholder { color: var(--text-dim); }
.filter-chips { display: flex; gap: 0.4rem; }
.chip {
    padding: 0.35rem 0.75rem; border-radius: 999px;
    border: 1px solid var(--border); background: transparent;
    color: var(--text-dim); font-size: 0.75rem; font-weight: 600;
    cursor: pointer; transition: all .15s; font-family: var(--font);
}
.chip:hover { border-color: var(--accent); color: var(--text); }
.chip.active { background: var(--accent-dim); border-color: var(--accent); color: var(--accent); }

/* ── Main ───────────────────────────────────────────────────── */
main { max-width: 1200px; margin: 0 auto; padding: 1.5rem 2rem; flex: 1; width: 100%; }
.section { margin-bottom: 2rem; }

/* ── Summary cards ──────────────────────────────────────────── */
.cards {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(120px, 1fr));
    gap: 0.75rem; margin-bottom: 1rem;
}
.card {
    background: var(--surface); border: 1px solid var(--border);
    border-radius: var(--radius); padding: 1rem; text-align: center;
    box-shadow: var(--shadow); transition: transform .15s, box-shadow .15s;
}
.card:hover { transform: translateY(-1px); box-shadow: var(--shadow-lg); }
.card-value {
    font-size: 1.6rem; font-weight: 700;
    font-family: var(--mono); line-height: 1.2;
}
.card-label {
    font-size: 0.75rem; color: var(--text-dim);
    text-transform: uppercase; letter-spacing: 0.06em; margin-top: 0.25rem;
}
.card-green .card-value { color: var(--green); }
.card-red   .card-value { color: var(--red); }
.card-yellow .card-value { color: var(--yellow); }

.progress-bar {
    display: flex; height: 10px; border-radius: 999px;
    overflow: hidden; background: var(--surface2);
}
.bar-seg { transition: width .3s ease; }

/* ── Performance section ────────────────────────────────────── */
.perf-grid {
    display: grid; grid-template-columns: repeat(3, 1fr);
    gap: 0.75rem; margin-bottom: 1rem;
}
.perf-card {
    background: var(--surface); border: 1px solid var(--border);
    border-radius: var(--radius-sm); padding: 0.75rem; text-align: center;
}
.perf-value {
    font-size: 1.2rem; font-weight: 700;
    font-family: var(--mono); color: var(--accent);
}
.perf-label {
    font-size: 0.7rem; color: var(--text-dim);
    text-transform: uppercase; letter-spacing: 0.05em; margin-top: 0.15rem;
}
.perf-subtitle {
    font-size: 0.85rem; color: var(--text-dim);
    margin-bottom: 0.5rem; font-weight: 600;
}
.slowest-table { margin-bottom: 0; }

/* ── Failure section ────────────────────────────────────────── */
.failures-section h2 { color: var(--red); margin-bottom: 0.75rem; font-size: 1.15rem; }
.failures-section .count { font-weight: 400; color: var(--text-dim); font-size: 0.9rem; }

.failure-card {
    background: var(--surface); border: 1px solid var(--border);
    border-left: 3px solid var(--red); border-radius: var(--radius);
    margin-bottom: 0.5rem; overflow: hidden; box-shadow: var(--shadow);
}
.failure-card summary {
    padding: 0.75rem 1rem; cursor: pointer;
    display: flex; align-items: center; gap: 0.5rem;
    user-select: none; font-size: 0.9rem; list-style: none;
}
.failure-card summary::-webkit-details-marker { display: none; }
.failure-card summary::before {
    content: "\\25b8"; font-size: 0.75rem; color: var(--text-dim);
    transition: transform .15s;
}
.failure-card[open] summary::before { transform: rotate(90deg); }
.failure-time {
    margin-left: auto; font-family: var(--mono);
    font-size: 0.75rem; color: var(--text-dim);
}
.failure-body {
    padding: 0 1rem 1rem 1rem; border-top: 1px solid var(--border);
}
.failure-reason-text { color: var(--red); }

.detail-row {
    display: flex; gap: 0.75rem; padding: 0.4rem 0;
    font-size: 0.85rem; align-items: baseline; flex-wrap: wrap;
}
.detail-label {
    font-weight: 600; color: var(--text-dim);
    text-transform: uppercase; font-size: 0.7rem;
    letter-spacing: 0.05em; min-width: 110px; flex-shrink: 0;
}
.detail-row code {
    font-family: var(--mono); font-size: 0.8rem;
    color: var(--accent); word-break: break-all;
}

/* ── Meta grid ──────────────────────────────────────────────── */
.meta-grid {
    display: flex; flex-wrap: wrap; gap: 0.5rem;
    padding: 0.5rem 0; margin: 0.25rem 0;
}
.meta-item {
    display: flex; gap: 0.35rem; align-items: baseline;
    font-size: 0.8rem;
    background: var(--surface2); border-radius: var(--radius-sm);
    padding: 0.25rem 0.6rem;
}
.meta-key {
    color: var(--text-dim); font-weight: 600;
    font-size: 0.7rem; text-transform: uppercase;
}
.meta-val { font-family: var(--mono); color: var(--text); font-size: 0.78rem; }

/* ── Expected vs Actual ─────────────────────────────────────── */
.comparison {
    display: grid; grid-template-columns: 1fr 1fr;
    gap: 0.75rem; margin: 0.5rem 0;
}
.comparison h4 {
    font-size: 0.75rem; color: var(--text-dim);
    text-transform: uppercase; letter-spacing: 0.05em; margin-bottom: 0.35rem;
}

/* ── Output blocks ──────────────────────────────────────────── */
.output-block {
    background: var(--surface3); border: 1px solid var(--border);
    border-radius: var(--radius-sm); padding: 0.75rem 1rem;
    font-family: var(--mono); font-size: 0.78rem; line-height: 1.5;
    overflow-x: auto; margin: 0.4rem 0 0.6rem 0;
    color: var(--text); white-space: pre-wrap; word-break: break-word;
    max-height: 300px; overflow-y: auto;
}
.output-block.compact { max-height: 120px; }
.output-block.stderr { border-left: 3px solid var(--red); }

/* ── Source code viewer ─────────────────────────────────────── */
.source-viewer {
    margin: 0.5rem 0; border: 1px solid var(--border);
    border-radius: var(--radius-sm); overflow: hidden;
}
.source-viewer summary {
    padding: 0.5rem 0.75rem; cursor: pointer;
    font-size: 0.8rem; font-weight: 600; color: var(--text-dim);
    background: var(--surface2);
    display: flex; align-items: center; gap: 0.5rem;
    user-select: none; list-style: none; transition: background .15s;
}
.source-viewer summary::-webkit-details-marker { display: none; }
.source-viewer summary::before {
    content: "\\25b8"; font-size: 0.7rem;
    transition: transform .15s; display: inline-block;
}
.source-viewer[open] summary::before { transform: rotate(90deg); }
.source-viewer summary:hover { background: var(--surface); }
.source-path {
    font-family: var(--mono); font-size: 0.75rem; color: var(--accent);
}

.source-block {
    background: #0b1120; margin: 0; padding: 0.75rem 0;
    font-family: var(--mono); font-size: 0.78rem; line-height: 1.6;
    overflow-x: auto; max-height: 500px; overflow-y: auto;
    color: var(--text); border: none; border-radius: 0;
}
.source-block code { display: block; }
.source-block .line {
    display: block; padding: 0 1rem 0 0; min-height: 1.6em;
}
.source-block .line:hover { background: rgba(56,189,248,.06); }
.source-block .ln {
    display: inline-block; min-width: 3ch;
    text-align: right; margin-right: 1.5ch;
    color: var(--text-dim); opacity: 0.4;
    user-select: none; padding-left: 0.75rem;
}

/* ── Syntax highlighting ────────────────────────────────────── */
.hl-keyword { color: #c084fc; font-weight: 600; }
.hl-type { color: #38bdf8; }
.hl-string { color: #86efac; }
.hl-comment { color: #64748b; font-style: italic; }
.hl-number { color: #fbbf24; }
.hl-arrow { color: #94a3b8; }
.hl-decorator { color: #f472b6; }

/* ── Category sections ──────────────────────────────────────── */
.category-header { cursor: pointer; user-select: none; }
.category-header h2 {
    font-size: 1.1rem; display: flex; align-items: center;
    gap: 0.5rem; color: var(--accent); padding: 0.5rem 0;
}
.category-header .chevron {
    font-size: 0.8rem; transition: transform .15s; display: inline-block;
}
.collapsed .category-header .chevron { transform: rotate(-90deg); }
.collapsed .test-list,
.collapsed .test-table,
.collapsed .perf-grid,
.collapsed .perf-subtitle,
.collapsed .slowest-table { display: none; }
.cat-stats {
    font-weight: 400; font-size: 0.8rem;
    color: var(--text-dim); margin-left: auto;
}

/* ── Test cards ─────────────────────────────────────────────── */
.test-list { display: flex; flex-direction: column; gap: 0.25rem; }
.test-card {
    background: var(--surface); border: 1px solid var(--border);
    border-radius: var(--radius-sm); overflow: hidden;
    transition: border-color .15s;
}
.test-card:hover { border-color: rgba(56,189,248,.3); }
.test-card-fail { border-left: 3px solid var(--red); }
.test-card-summary {
    padding: 0.5rem 0.75rem; cursor: pointer;
    display: flex; align-items: center; gap: 0.5rem;
    font-size: 0.85rem; list-style: none;
}
.test-card-summary::-webkit-details-marker { display: none; }
.test-card-summary::before {
    content: "\\25b8"; font-size: 0.65rem; color: var(--text-dim);
    transition: transform .15s; flex-shrink: 0;
}
.test-card[open] .test-card-summary::before { transform: rotate(90deg); }
.test-card-summary .test-name {
    font-family: var(--mono); font-size: 0.82rem; flex-shrink: 0;
}
.test-card-summary .test-time {
    margin-left: auto; font-family: var(--mono);
    color: var(--text-dim); font-size: 0.78rem; flex-shrink: 0;
}
.test-card-summary .tags-group { display: flex; gap: 0.2rem; flex-wrap: wrap; }
.test-card-body {
    padding: 0.75rem; border-top: 1px solid var(--border);
    background: rgba(15,23,42,.4);
}
.test-description {
    font-size: 0.85rem; color: var(--text-dim);
    margin-bottom: 0.5rem; font-style: italic;
}
.meta-chips { display: flex; flex-wrap: wrap; gap: 0.35rem; margin-bottom: 0.5rem; }
.meta-chip {
    font-size: 0.7rem; font-family: var(--mono);
    background: var(--surface2); color: var(--text-dim);
    padding: 0.15rem 0.5rem; border-radius: 4px;
}
.expected-section h4 {
    font-size: 0.75rem; color: var(--text-dim);
    text-transform: uppercase; letter-spacing: 0.05em; margin-bottom: 0.25rem;
}

/* ── Test table (performance slowest) ───────────────────────── */
.test-table {
    width: 100%; border-collapse: collapse; font-size: 0.85rem;
}
.test-table th {
    text-align: left; font-weight: 600; color: var(--text-dim);
    font-size: 0.7rem; text-transform: uppercase;
    letter-spacing: 0.05em; padding: 0.5rem 0.75rem;
    border-bottom: 1px solid var(--border);
}
.test-table td {
    padding: 0.5rem 0.75rem;
    border-bottom: 1px solid var(--border-dim);
    vertical-align: middle;
}
.test-table tbody tr:hover { background: rgba(56,189,248,.04); }
.test-table .test-name { font-family: var(--mono); font-size: 0.82rem; }
.test-table .test-time {
    font-family: var(--mono); color: var(--text-dim);
    font-size: 0.8rem; text-align: right;
}

/* ── Badge ──────────────────────────────────────────────────── */
.badge {
    display: inline-block; padding: 0.15rem 0.5rem;
    border-radius: 4px; font-weight: 700;
    font-size: 0.7rem; font-family: var(--mono);
    letter-spacing: 0.04em; text-transform: uppercase; flex-shrink: 0;
}

/* ── Tags ───────────────────────────────────────────────────── */
.tag {
    display: inline-block; padding: 0.1rem 0.4rem;
    border-radius: 4px; font-size: 0.65rem;
    font-family: var(--mono); background: var(--surface2);
    color: var(--text-dim);
}
.xfail-reason, .skip-reason {
    font-size: 0.75rem; color: var(--text-dim); font-style: italic;
}

/* ── Footer ─────────────────────────────────────────────────── */
footer {
    text-align: center; padding: 1.5rem 2rem;
    font-size: 0.75rem; color: var(--text-dim);
    border-top: 1px solid var(--border);
}

/* ── Responsive ─────────────────────────────────────────────── */
@media (max-width: 640px) {
    main, header, .toolbar { padding-left: 1rem; padding-right: 1rem; }
    .cards { grid-template-columns: repeat(auto-fill, minmax(90px, 1fr)); }
    .perf-grid { grid-template-columns: 1fr; }
    .comparison { grid-template-columns: 1fr; }
    .test-card-summary { font-size: 0.78rem; }
}

/* ── Print ──────────────────────────────────────────────────── */
@media print {
    body { background: #fff; color: #111; }
    header { background: #fff; border-color: #ddd; }
    .toolbar { display: none; }
    .logo-row h1 { color: #0284c7; }
    .card { border-color: #ddd; background: #f8fafc; box-shadow: none; }
    .failure-card { border-color: #ddd; background: #fff; box-shadow: none; }
    .output-block { background: #f1f5f9; border-color: #ddd; }
    .source-block { background: #f8fafc; }
    .hl-keyword { color: #7c3aed; }
    .hl-type { color: #0284c7; }
    .hl-string { color: #16a34a; }
    .hl-comment { color: #94a3b8; }
    .hl-number { color: #d97706; }
    footer { border-color: #ddd; }
}
"""

_JS = """\
// ── Search & Filter ─────────────────────────────────────────
function filterTests(query) {
    var q = query.toLowerCase();
    document.querySelectorAll('.test-card, .failure-card').forEach(function(card) {
        var name = card.getAttribute('data-test-name') || '';
        card.style.display = (!q || name.indexOf(q) !== -1) ? '' : 'none';
    });
    document.querySelectorAll('.category-section').forEach(function(sec) {
        var visible = sec.querySelectorAll('.test-card:not([style*=\"display: none\"])');
        sec.style.display = (visible.length > 0 || !q) ? '' : 'none';
    });
}

function filterByVerdict(verdict, btn) {
    document.querySelectorAll('.filter-chips .chip').forEach(function(c) {
        c.classList.remove('active');
    });
    btn.classList.add('active');
    document.getElementById('search-input').value = '';

    document.querySelectorAll('.test-card').forEach(function(card) {
        if (verdict === 'all') {
            card.style.display = '';
        } else {
            var v = card.getAttribute('data-verdict') || '';
            var show = false;
            if (verdict === 'fail') {
                show = (v === 'fail' || v === 'error' || v === 'timeout' || v === 'xpass');
            } else if (verdict === 'skip') {
                show = (v === 'skip' || v === 'xfail');
            } else {
                show = (v === verdict);
            }
            card.style.display = show ? '' : 'none';
        }
    });

    var failSection = document.querySelector('.failures-section');
    if (failSection) {
        failSection.style.display = (verdict === 'all' || verdict === 'fail') ? '' : 'none';
    }

    document.querySelectorAll('.category-section').forEach(function(sec) {
        var visible = sec.querySelectorAll('.test-card:not([style*=\"display: none\"])');
        sec.style.display = visible.length > 0 ? '' : 'none';
    });
}

// ── Auto-expand first failure ───────────────────────────────
document.addEventListener("DOMContentLoaded", function() {
    var first = document.querySelector(".failure-card");
    if (first) first.open = true;
});
"""