"""Test discovery: walks e2e/ directories, parses metadata, returns TestCase list."""

from __future__ import annotations

import re
from pathlib import Path

from .core import TestCase, TestMetadata, parse_metadata


_TIER_RE = re.compile(r"tier(\d+)")


def _infer_tier(directory_name: str) -> int | None:
    """Infer tier number from directory name like 'tier1_core'."""
    m = _TIER_RE.search(directory_name)
    return int(m.group(1)) if m else None


def _infer_category(source_path: Path, e2e_root: Path) -> str:
    """Infer category from the immediate subdirectory under e2e/."""
    try:
        relative = source_path.parent.relative_to(e2e_root)
        parts = relative.parts
        return parts[0] if parts else "uncategorized"
    except ValueError:
        return "uncategorized"


def discover_tests(e2e_root: Path) -> list[TestCase]:
    """Walk e2e/ recursively, parse metadata, validate, and return sorted test cases."""
    if not e2e_root.is_dir():
        raise FileNotFoundError(f"E2E test directory not found: {e2e_root}")

    tests: list[TestCase] = []
    seen_names: dict[str, Path] = {}
    errors: list[str] = []

    for cryo_file in sorted(e2e_root.rglob("*.cryo")):
        meta = parse_metadata(cryo_file)

        # Validate required keys
        if not meta.name:
            errors.append(f"{cryo_file}: missing @test metadata")
            continue
        if meta.expect is None:
            errors.append(f"{cryo_file}: missing @expect metadata")
            continue

        # Check for duplicate names
        if meta.name in seen_names:
            errors.append(
                f"{cryo_file}: duplicate test name '{meta.name}' "
                f"(first seen in {seen_names[meta.name]})"
            )
            continue
        seen_names[meta.name] = cryo_file

        # Infer category and tier from directory
        category = _infer_category(cryo_file, e2e_root)
        if meta.tier is None:
            meta.tier = _infer_tier(category)

        tests.append(TestCase(
            source_path=cryo_file,
            metadata=meta,
            category=category,
        ))

    if errors:
        msg = "Test discovery errors:\n" + "\n".join(f"  - {e}" for e in errors)
        raise ValueError(msg)

    return tests
