"""Parallel test execution using ProcessPoolExecutor."""

from __future__ import annotations

import os
import traceback
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

from .core import (
    CompilationResult,
    ExpectResult,
    TestCase,
    TestResult,
    TestVerdict,
)
from .compiler import CompilerDriver
from .executor import BinaryExecutor


def _indent(text: str, prefix: str = "    ") -> str:
    """Indent each line of text for readable failure output."""
    return "\n".join(prefix + line for line in text.rstrip("\n").split("\n"))


def _run_single_test(
    cryo_binary: str,
    source_path: str,
    metadata_dict: dict,
    category: str,
    build_dir: str,
) -> dict:
    """Run a single test in a worker process. Uses dicts for pickling."""
    from .core import (
        CompileMode,
        ExpectResult,
        TestCase,
        TestMetadata,
        TestVerdict,
    )

    # Reconstruct objects from dicts
    meta = TestMetadata(
        name=metadata_dict["name"],
        expect=ExpectResult(metadata_dict["expect"]),
        description=metadata_dict.get("description", ""),
        mode=CompileMode(metadata_dict.get("mode", "raw")),
        exit_code=metadata_dict.get("exit_code", 0),
        stdout_lines=metadata_dict.get("stdout_lines", []),
        stdout_contains=metadata_dict.get("stdout_contains", []),
        stderr_contains=metadata_dict.get("stderr_contains", []),
        error_code=metadata_dict.get("error_code", ""),
        tags=metadata_dict.get("tags", []),
        tier=metadata_dict.get("tier"),
        timeout=metadata_dict.get("timeout", 10),
        compile_timeout=metadata_dict.get("compile_timeout", 30),
        skip=metadata_dict.get("skip", ""),
        xfail=metadata_dict.get("xfail", ""),
        compiler_args=metadata_dict.get("compiler_args", ""),
        run_args=metadata_dict.get("run_args", ""),
    )
    test = TestCase(source_path=Path(source_path), metadata=meta, category=category)

    result = run_test(test, Path(cryo_binary), Path(build_dir))

    # Convert result to dict for pickling
    return {
        "test_name": result.test.metadata.name,
        "verdict": result.verdict.value,
        "failure_reason": result.failure_reason,
        "duration_ms": result.duration_ms,
        "compilation": {
            "exit_code": result.compilation.exit_code,
            "stdout": result.compilation.stdout,
            "stderr": result.compilation.stderr,
            "duration_ms": result.compilation.duration_ms,
            "output_binary": str(result.compilation.output_binary) if result.compilation.output_binary else None,
        } if result.compilation else None,
        "execution": {
            "exit_code": result.execution.exit_code,
            "stdout": result.execution.stdout,
            "stderr": result.execution.stderr,
            "duration_ms": result.execution.duration_ms,
        } if result.execution else None,
    }


def run_test(test: TestCase, cryo_binary: Path, build_dir: Path) -> TestResult:
    """Run a single test case through compilation and optional execution."""
    meta = test.metadata

    # Step 1: Skip check
    if meta.skip:
        return TestResult(
            test=test,
            verdict=TestVerdict.SKIP,
            failure_reason=f"Skipped: {meta.skip}",
        )

    driver = CompilerDriver(cryo_binary)
    executor = BinaryExecutor()

    # Step 2: Compile
    output_dir = build_dir / meta.name
    comp = driver.compile(test, output_dir)

    if comp.stderr == "Compilation timed out":
        result = TestResult(
            test=test,
            verdict=TestVerdict.TIMEOUT,
            compilation=comp,
            failure_reason="Compilation timed out",
            duration_ms=comp.duration_ms,
        )
        return _apply_xfail(result, meta.xfail)

    # Step 3: Check expect
    if meta.expect == ExpectResult.COMPILE_FAIL:
        return _check_compile_fail(test, comp)

    # expect == compile_success
    if comp.exit_code != 0:
        result = TestResult(
            test=test,
            verdict=TestVerdict.FAIL,
            compilation=comp,
            failure_reason=f"Compilation failed (exit code {comp.exit_code})",
            duration_ms=comp.duration_ms,
        )
        return _apply_xfail(result, meta.xfail)

    if comp.output_binary is None:
        result = TestResult(
            test=test,
            verdict=TestVerdict.FAIL,
            compilation=comp,
            failure_reason="Compilation succeeded but no output binary found",
            duration_ms=comp.duration_ms,
        )
        return _apply_xfail(result, meta.xfail)

    # Make binary executable
    comp.output_binary.chmod(0o755)

    # Step 4: Execute
    exe = executor.execute(comp.output_binary, meta.run_args, meta.timeout)

    total_ms = comp.duration_ms + exe.duration_ms

    if exe.stderr == "Execution timed out":
        result = TestResult(
            test=test,
            verdict=TestVerdict.TIMEOUT,
            compilation=comp,
            execution=exe,
            failure_reason="Execution timed out",
            duration_ms=total_ms,
        )
        return _apply_xfail(result, meta.xfail)

    # Step 5: Validate execution results
    # Check exit code
    if exe.exit_code != meta.exit_code:
        result = TestResult(
            test=test,
            verdict=TestVerdict.FAIL,
            compilation=comp,
            execution=exe,
            failure_reason=f"Exit code: expected {meta.exit_code}, got {exe.exit_code}",
            duration_ms=total_ms,
        )
        return _apply_xfail(result, meta.xfail)

    # Check stdout exact match
    if meta.stdout_lines:
        expected_stdout = "\n".join(meta.stdout_lines) + "\n"
        actual_stdout = exe.stdout
        if actual_stdout != expected_stdout:
            result = TestResult(
                test=test,
                verdict=TestVerdict.FAIL,
                compilation=comp,
                execution=exe,
                failure_reason=f"Stdout mismatch:\n  expected:\n{_indent(expected_stdout)}\n  actual:\n{_indent(actual_stdout)}",
                duration_ms=total_ms,
            )
            return _apply_xfail(result, meta.xfail)

    # Check stdout contains
    for substr in meta.stdout_contains:
        if substr not in exe.stdout:
            result = TestResult(
                test=test,
                verdict=TestVerdict.FAIL,
                compilation=comp,
                execution=exe,
                failure_reason=f"Stdout missing substring: {substr}",
                duration_ms=total_ms,
            )
            return _apply_xfail(result, meta.xfail)

    result = TestResult(
        test=test,
        verdict=TestVerdict.PASS,
        compilation=comp,
        execution=exe,
        duration_ms=total_ms,
    )
    return _apply_xfail(result, meta.xfail)


def _check_compile_fail(test: TestCase, comp: CompilationResult) -> TestResult:
    """Validate a test expected to fail compilation."""
    meta = test.metadata

    if comp.exit_code == 0:
        result = TestResult(
            test=test,
            verdict=TestVerdict.FAIL,
            compilation=comp,
            failure_reason="Expected compile_fail but compilation succeeded",
            duration_ms=comp.duration_ms,
        )
        return _apply_xfail(result, meta.xfail)

    # Check error code in stderr
    if meta.error_code and meta.error_code not in comp.stderr:
        result = TestResult(
            test=test,
            verdict=TestVerdict.FAIL,
            compilation=comp,
            failure_reason=f"Expected error code {meta.error_code} not found in stderr",
            duration_ms=comp.duration_ms,
        )
        return _apply_xfail(result, meta.xfail)

    # Check stderr contains
    for substr in meta.stderr_contains:
        if substr not in comp.stderr:
            result = TestResult(
                test=test,
                verdict=TestVerdict.FAIL,
                compilation=comp,
                failure_reason=f"Stderr missing substring: {substr!r}",
                duration_ms=comp.duration_ms,
            )
            return _apply_xfail(result, meta.xfail)

    result = TestResult(
        test=test,
        verdict=TestVerdict.PASS,
        compilation=comp,
        duration_ms=comp.duration_ms,
    )
    return _apply_xfail(result, meta.xfail)


def _apply_xfail(result: TestResult, xfail_reason: str) -> TestResult:
    """Apply xfail logic: FAIL→XFAIL, PASS→XPASS."""
    if not xfail_reason:
        return result
    if result.verdict == TestVerdict.FAIL:
        result.verdict = TestVerdict.XFAIL
        result.failure_reason = f"Expected failure: {xfail_reason}"
    elif result.verdict == TestVerdict.PASS:
        result.verdict = TestVerdict.XPASS
        result.failure_reason = f"Unexpectedly passed (expected failure: {xfail_reason})"
    return result


def run_tests_parallel(
    tests: list[TestCase],
    cryo_binary: Path,
    build_dir: Path,
    max_workers: int | None = None,
) -> list[TestResult]:
    """Run tests in parallel using ProcessPoolExecutor."""
    if max_workers is None:
        max_workers = min(os.cpu_count() or 1, len(tests), 8)

    # Serialize test data for pickling
    futures = {}
    with ProcessPoolExecutor(max_workers=max_workers) as pool:
        for test in tests:
            meta = test.metadata
            meta_dict = {
                "name": meta.name,
                "expect": meta.expect.value,
                "description": meta.description,
                "mode": meta.mode.value,
                "exit_code": meta.exit_code,
                "stdout_lines": meta.stdout_lines,
                "stdout_contains": meta.stdout_contains,
                "stderr_contains": meta.stderr_contains,
                "error_code": meta.error_code,
                "tags": meta.tags,
                "tier": meta.tier,
                "timeout": meta.timeout,
                "compile_timeout": meta.compile_timeout,
                "skip": meta.skip,
                "xfail": meta.xfail,
                "compiler_args": meta.compiler_args,
                "run_args": meta.run_args,
            }
            worker_dir = build_dir / f"worker_{hash(meta.name) % max_workers}"
            future = pool.submit(
                _run_single_test,
                str(cryo_binary),
                str(test.source_path),
                meta_dict,
                test.category,
                str(worker_dir),
            )
            futures[future] = test

    # Collect results preserving test order
    result_map: dict[str, TestResult] = {}
    for future in as_completed(futures):
        test = futures[future]
        try:
            rd = future.result()
            # Reconstruct TestResult from dict
            comp = None
            if rd["compilation"]:
                c = rd["compilation"]
                comp = CompilationResult(
                    exit_code=c["exit_code"],
                    stdout=c["stdout"],
                    stderr=c["stderr"],
                    duration_ms=c["duration_ms"],
                    output_binary=Path(c["output_binary"]) if c["output_binary"] else None,
                )
            exe = None
            if rd["execution"]:
                e = rd["execution"]
                from .core import ExecutionResult
                exe = ExecutionResult(
                    exit_code=e["exit_code"],
                    stdout=e["stdout"],
                    stderr=e["stderr"],
                    duration_ms=e["duration_ms"],
                )
            result_map[test.metadata.name] = TestResult(
                test=test,
                verdict=TestVerdict(rd["verdict"]),
                compilation=comp,
                execution=exe,
                failure_reason=rd["failure_reason"],
                duration_ms=rd["duration_ms"],
            )
        except Exception as e:
            result_map[test.metadata.name] = TestResult(
                test=test,
                verdict=TestVerdict.ERROR,
                failure_reason=f"Worker error: {e}\n{traceback.format_exc()}",
            )

    return [result_map[t.metadata.name] for t in tests]


def run_tests_sequential(
    tests: list[TestCase],
    cryo_binary: Path,
    build_dir: Path,
    verbose: bool = False,
) -> list[TestResult]:
    """Run tests sequentially (for debugging)."""
    results: list[TestResult] = []
    for test in tests:
        if verbose:
            print(f"  Running: {test.metadata.name}...", end=" ", flush=True)
        result = run_test(test, cryo_binary, build_dir)
        if verbose:
            print(result.verdict.value)
        results.append(result)
    return results
