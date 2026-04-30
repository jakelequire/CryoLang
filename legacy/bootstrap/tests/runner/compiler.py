"""CompilerDriver: wraps ./bin/cryo invocations."""

from __future__ import annotations

import shlex
import subprocess
import sys
import time
from pathlib import Path

from .core import CompilationResult, CompileMode, TestCase
from .discovery import parse_cryoconfig


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
            cmd.append("--emit-llvm")

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

    def compile_project(self, test: TestCase) -> CompilationResult:
        """Build a project-mode test.

        Uses `cryo build` with cwd=project_dir.  The build command reads
        cryoconfig and handles no_std mode internally.
        """
        project_dir = test.project_dir
        if project_dir is None:
            return CompilationResult(
                exit_code=-1,
                stdout="",
                stderr="project_dir is None for project-mode test",
                duration_ms=0.0,
            )

        config = parse_cryoconfig(project_dir / "cryoconfig")
        output_dir_name = config.get("output_dir", "build")
        project_name = config.get("project_name", "app")

        cmd = [str(self.cryo_binary), "build"]
        if test.metadata.compiler_args:
            cmd.extend(shlex.split(test.metadata.compiler_args))
        output_path = project_dir / output_dir_name / project_name

        start = time.monotonic()
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                cwd=str(project_dir),
                timeout=test.metadata.compile_timeout,
            )
        except subprocess.TimeoutExpired:
            duration_ms = (time.monotonic() - start) * 1000
            return CompilationResult(
                exit_code=-1,
                stdout="",
                stderr="Compilation timed out",
                duration_ms=duration_ms,
            )
        except Exception as e:
            duration_ms = (time.monotonic() - start) * 1000
            return CompilationResult(
                exit_code=-1,
                stdout="",
                stderr=f"Compilation error: {e}",
                duration_ms=duration_ms,
            )

        duration_ms = (time.monotonic() - start) * 1000

        # Locate output binary
        binary = None
        if result.returncode == 0:
            for candidate in [output_path, output_path.with_suffix(".exe"), output_path.with_suffix(".out")]:
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
