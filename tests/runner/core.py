"""Core data model and metadata parser for the Cryo E2E test suite."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Optional


class ExpectResult(Enum):
    COMPILE_SUCCESS = "compile_success"
    COMPILE_FAIL = "compile_fail"


class CompileMode(Enum):
    RAW = "raw"
    STDLIB = "stdlib"


class TestVerdict(Enum):
    PASS = "PASS"
    FAIL = "FAIL"
    SKIP = "SKIP"
    XFAIL = "XFAIL"
    XPASS = "XPASS"
    ERROR = "ERROR"
    TIMEOUT = "TIMEOUT"


@dataclass
class TestMetadata:
    name: str = ""
    expect: Optional[ExpectResult] = None
    description: str = ""
    mode: CompileMode = CompileMode.RAW
    exit_code: int = 0
    stdout_lines: list[str] = field(default_factory=list)
    stdout_contains: list[str] = field(default_factory=list)
    stderr_contains: list[str] = field(default_factory=list)
    error_code: str = ""
    tags: list[str] = field(default_factory=list)
    tier: Optional[int] = None
    timeout: int = 10
    compile_timeout: int = 30
    skip: str = ""
    xfail: str = ""
    compiler_args: str = ""
    run_args: str = ""


@dataclass
class TestCase:
    source_path: Path
    metadata: TestMetadata
    category: str = ""


@dataclass
class CompilationResult:
    exit_code: int
    stdout: str
    stderr: str
    duration_ms: float
    output_binary: Optional[Path] = None


@dataclass
class ExecutionResult:
    exit_code: int
    stdout: str
    stderr: str
    duration_ms: float


@dataclass
class TestResult:
    test: TestCase
    verdict: TestVerdict
    compilation: Optional[CompilationResult] = None
    execution: Optional[ExecutionResult] = None
    failure_reason: str = ""
    duration_ms: float = 0.0


_METADATA_RE = re.compile(r"^//\s*@(\w+):\s*(.+)$")


def _unescape(s: str) -> str:
    """Process escape sequences in metadata values."""
    return s.replace("\\n", "\n").replace("\\t", "\t")


def parse_metadata(file_path: Path) -> TestMetadata:
    """Parse // @key: value metadata from the top of a .cryo file."""
    meta = TestMetadata()
    with open(file_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            m = _METADATA_RE.match(line)
            if not m:
                # Stop at first non-metadata, non-empty, non-comment line
                stripped = line.strip()
                if stripped and not stripped.startswith("//"):
                    break
                continue

            key, value = m.group(1), m.group(2).strip()

            if key == "test":
                meta.name = value
            elif key == "expect":
                meta.expect = ExpectResult(value)
            elif key == "description":
                meta.description = value
            elif key == "mode":
                meta.mode = CompileMode(value)
            elif key == "exit":
                meta.exit_code = int(value)
            elif key == "stdout":
                meta.stdout_lines.append(_unescape(value))
            elif key == "stdout_contains":
                meta.stdout_contains.append(_unescape(value))
            elif key == "stderr_contains":
                meta.stderr_contains.append(_unescape(value))
            elif key == "error_code":
                meta.error_code = value
            elif key == "tags":
                meta.tags = [t.strip() for t in value.split(",") if t.strip()]
            elif key == "tier":
                meta.tier = int(value)
            elif key == "timeout":
                meta.timeout = int(value)
            elif key == "compile_timeout":
                meta.compile_timeout = int(value)
            elif key == "skip":
                meta.skip = value
            elif key == "xfail":
                meta.xfail = value
            elif key == "compiler_args":
                meta.compiler_args = value
            elif key == "run_args":
                meta.run_args = value

    return meta
