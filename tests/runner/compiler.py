"""CompilerDriver: wraps ./bin/cryo invocations."""

from __future__ import annotations

import shlex
import subprocess
import sys
import time
from pathlib import Path

from .core import CompilationResult, CompileMode, TestCase


class CompilerDriver:
    def __init__(self, cryo_binary: Path):
        self.cryo_binary = cryo_binary

    def compile(self, test: TestCase, output_dir: Path) -> CompilationResult:
        """Compile a .cryo test file and return the result."""
        output_dir.mkdir(parents=True, exist_ok=True)
        output_path = output_dir / test.metadata.name
        if sys.platform == "win32":
            output_path = output_path.with_suffix(".exe")

        cmd = [str(self.cryo_binary), str(test.source_path)]

        if test.metadata.mode == CompileMode.RAW:
            cmd.append("--raw")

        cmd.extend(["-o", str(output_path)])

        if test.metadata.compiler_args:
            cmd.extend(shlex.split(test.metadata.compiler_args))

        start = time.monotonic()
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=test.metadata.compile_timeout,
            )
        except subprocess.TimeoutExpired:
            duration_ms = (time.monotonic() - start) * 1000
            return CompilationResult(
                exit_code=-1,
                stdout="",
                stderr="Compilation timed out",
                duration_ms=duration_ms,
                output_binary=None,
            )
        except Exception as e:
            duration_ms = (time.monotonic() - start) * 1000
            return CompilationResult(
                exit_code=-1,
                stdout="",
                stderr=f"Compilation error: {e}",
                duration_ms=duration_ms,
                output_binary=None,
            )

        duration_ms = (time.monotonic() - start) * 1000

        # Check for output binary existence
        binary = None
        if result.returncode == 0:
            # The compiler may produce the binary at the exact output_path or with an extension
            for candidate in [output_path, output_path.with_suffix(".exe"), output_path.with_suffix(".out"), output_path.with_suffix("")]:
                if candidate.exists() and candidate.is_file():
                    binary = candidate
                    break

        return CompilationResult(
            exit_code=result.returncode,
            stdout=result.stdout,
            stderr=result.stderr,
            duration_ms=duration_ms,
            output_binary=binary,
        )
