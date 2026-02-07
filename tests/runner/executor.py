"""BinaryExecutor: runs compiled binaries and captures output."""

from __future__ import annotations

import shlex
import subprocess
import time
from pathlib import Path

from .core import ExecutionResult


class BinaryExecutor:
    def execute(self, binary_path: Path, args: str = "", timeout: int = 10) -> ExecutionResult:
        """Run a compiled binary and return the result."""
        cmd = [str(binary_path)]
        if args:
            cmd.extend(shlex.split(args))

        start = time.monotonic()
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=timeout,
            )
        except subprocess.TimeoutExpired:
            duration_ms = (time.monotonic() - start) * 1000
            return ExecutionResult(
                exit_code=-1,
                stdout="",
                stderr="Execution timed out",
                duration_ms=duration_ms,
            )
        except Exception as e:
            duration_ms = (time.monotonic() - start) * 1000
            return ExecutionResult(
                exit_code=-1,
                stdout="",
                stderr=f"Execution error: {e}",
                duration_ms=duration_ms,
            )

        duration_ms = (time.monotonic() - start) * 1000
        return ExecutionResult(
            exit_code=result.returncode,
            stdout=result.stdout,
            stderr=result.stderr,
            duration_ms=duration_ms,
        )
