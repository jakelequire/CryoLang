#!/usr/bin/env python3
"""Cryo Compiler E2E Test Runner - Main entry point."""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

# Add tests/ to path so runner package is importable
sys.path.insert(0, str(Path(__file__).parent))

from runner.core import TestCase
from runner.discovery import discover_tests
from runner.filters import filter_tests
from runner.parallel import run_tests_parallel, run_tests_sequential
from runner.reporter import ConsoleReporter


def find_project_root() -> Path:
    """Find the project root by walking up from this script."""
    p = Path(__file__).resolve().parent
    while p != p.parent:
        if (p / "makefile").exists() and (p / "src").is_dir():
            return p
        p = p.parent
    return Path(__file__).resolve().parent.parent


def main() -> int:
    project_root = find_project_root()

    parser = argparse.ArgumentParser(
        description="Cryo Compiler E2E Test Runner",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Examples:
  python3 tests/run_tests.py                        # Run all tests
  python3 tests/run_tests.py --tier 1               # Tier 1 only
  python3 tests/run_tests.py --category negative     # Negative tests only
  python3 tests/run_tests.py --name hello_world      # Single test by name
  python3 tests/run_tests.py --tags structs          # Filter by tag
  python3 tests/run_tests.py --workers 4             # Set parallelism
  python3 tests/run_tests.py --no-parallel           # Sequential mode
  python3 tests/run_tests.py --verbose               # Verbose output
  python3 tests/run_tests.py --list                  # List tests without running
""",
    )

    parser.add_argument("--cryo", type=str, default=None,
                        help="Path to cryo binary (default: ./bin/cryo)")
    parser.add_argument("--build-dir", type=str, default=None,
                        help="Build directory for test artifacts (default: tests/.build)")
    parser.add_argument("--e2e-dir", type=str, default=None,
                        help="E2E test directory (default: tests/e2e)")

    # Filtering
    parser.add_argument("--name", type=str, default=None,
                        help="Filter by test name (glob pattern)")
    parser.add_argument("--tier", type=int, default=None,
                        help="Filter by tier number")
    parser.add_argument("--tags", type=str, nargs="+", default=None,
                        help="Filter by tags (any match)")
    parser.add_argument("--exclude-tags", type=str, nargs="+", default=None,
                        help="Exclude tests with these tags")
    parser.add_argument("--category", type=str, default=None,
                        help="Filter by category (directory name)")

    # Execution
    parser.add_argument("--workers", type=int, default=None,
                        help="Number of parallel workers")
    parser.add_argument("--no-parallel", action="store_true",
                        help="Run tests sequentially")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--no-color", action="store_true",
                        help="Disable colored output")

    # List mode
    parser.add_argument("--list", action="store_true",
                        help="List discovered tests without running them")

    args = parser.parse_args()

    # Resolve paths
    cryo_binary = Path(args.cryo) if args.cryo else project_root / "bin" / "cryo"
    build_dir = Path(args.build_dir) if args.build_dir else project_root / "tests" / ".build"
    e2e_dir = Path(args.e2e_dir) if args.e2e_dir else project_root / "tests" / "e2e"

    # Append executable extension on Windows if not provided
    if sys.platform == "win32" and cryo_binary.suffix != ".exe":
        cryo_binary = cryo_binary.with_suffix(".exe")

    # Discover tests
    try:
        tests = discover_tests(e2e_dir)
    except (FileNotFoundError, ValueError) as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    # Apply filters
    tests = filter_tests(
        tests,
        name=args.name,
        tier=args.tier,
        tags=args.tags,
        exclude_tags=args.exclude_tags,
        category=args.category,
    )

    if not tests:
        print("No tests matched the given filters.")
        return 0

    # List mode
    if args.list:
        print(f"Discovered {len(tests)} test(s):\n")
        current_cat = ""
        for t in tests:
            if t.category != current_cat:
                current_cat = t.category
                print(f"  [{current_cat}]")
            tags_str = f" [{', '.join(t.metadata.tags)}]" if t.metadata.tags else ""
            skip_str = f" (SKIP: {t.metadata.skip})" if t.metadata.skip else ""
            xfail_str = f" (XFAIL: {t.metadata.xfail})" if t.metadata.xfail else ""
            expect_str = t.metadata.expect.value if t.metadata.expect else "?"
            print(f"    {t.metadata.name} ({expect_str}){tags_str}{skip_str}{xfail_str}")
        return 0

    # Check compiler exists
    if not cryo_binary.exists():
        print(f"Error: Cryo binary not found at {cryo_binary}", file=sys.stderr)
        print("Run 'make build' first.", file=sys.stderr)
        return 1

    # Run tests
    print(f"Running {len(tests)} test(s)...")
    build_dir.mkdir(parents=True, exist_ok=True)

    start = time.monotonic()

    if args.no_parallel or len(tests) == 1:
        results = run_tests_sequential(tests, cryo_binary, build_dir, verbose=args.verbose)
    else:
        results = run_tests_parallel(tests, cryo_binary, build_dir, max_workers=args.workers)

    elapsed = (time.monotonic() - start) * 1000

    # Report
    reporter = ConsoleReporter(verbose=args.verbose, color=not args.no_color)
    exit_code = reporter.report(results)

    # Write detailed results file to build dir
    report_path = reporter.write_report(results, build_dir, elapsed)
    print(f"\nResults written to: {report_path}")
    print(f"Total time: {elapsed:.0f}ms")

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
