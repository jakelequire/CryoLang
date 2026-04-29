"""Test discovery: walks e2e/ directories, parses metadata, returns TestCase list."""

from __future__ import annotations

import configparser
import re
from pathlib import Path
from typing import Optional

from .core import CompileMode, TestCase, TestMetadata, parse_metadata


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


def parse_cryoconfig(config_path: Path) -> dict[str, str]:
    """Minimal INI parse of a cryoconfig file.

    Returns dict with keys: project_name, output_dir, entry_point, no_std.
    """
    cp = configparser.ConfigParser()
    cp.read(config_path)
    result: dict[str, str] = {}
    if cp.has_section("project"):
        result["project_name"] = cp.get("project", "project_name", fallback="app").strip('"').strip("'")
        result["output_dir"] = cp.get("project", "output_dir", fallback="build").strip('"').strip("'")
        result["entry_point"] = cp.get("project", "entry_point", fallback="src/main.cryo").strip('"').strip("'")
    if cp.has_section("compiler"):
        result["no_std"] = cp.get("compiler", "no_std", fallback="false").strip().lower()
    return result


def _find_project_dirs(e2e_root: Path) -> dict[Path, Path]:
    """Find directories containing a cryoconfig file.

    Returns {project_dir: entry_point_path} for each project found.
    """
    projects: dict[Path, Path] = {}
    for config_file in sorted(e2e_root.rglob("cryoconfig")):
        project_dir = config_file.parent
        parsed = parse_cryoconfig(config_file)
        entry_point_rel = parsed.get("entry_point", "src/main.cryo")
        entry_point = project_dir / entry_point_rel
        projects[project_dir] = entry_point
    return projects


def _find_enclosing_project(
    file_path: Path,
    project_dirs: dict[Path, Path],
) -> Optional[tuple[Path, Path]]:
    """Check if a .cryo file lives inside a project directory.

    Returns (project_dir, entry_point_path) if found, else None.
    """
    for project_dir, entry_point in project_dirs.items():
        try:
            file_path.relative_to(project_dir)
            return project_dir, entry_point
        except ValueError:
            continue
    return None


def discover_tests(e2e_root: Path) -> list[TestCase]:
    """Walk e2e/ recursively, parse metadata, validate, and return sorted test cases."""
    if not e2e_root.is_dir():
        raise FileNotFoundError(f"E2E test directory not found: {e2e_root}")

    # Build the set of project directories before walking .cryo files
    project_dirs = _find_project_dirs(e2e_root)

    tests: list[TestCase] = []
    seen_names: dict[str, Path] = {}
    errors: list[str] = []

    for cryo_file in sorted(e2e_root.rglob("*.cryo")):
        # Check if this file is inside a project directory
        enclosing = _find_enclosing_project(cryo_file, project_dirs)
        if enclosing is not None:
            project_dir, entry_point = enclosing
            if cryo_file.resolve() != entry_point.resolve():
                # Non-entry-point file inside a project — skip silently
                continue

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

        # Set project_dir for project-mode tests
        project_dir_value = None
        if enclosing is not None:
            project_dir_value = enclosing[0]

        tests.append(TestCase(
            source_path=cryo_file,
            metadata=meta,
            category=category,
            project_dir=project_dir_value,
        ))

    if errors:
        msg = "Test discovery errors:\n" + "\n".join(f"  - {e}" for e in errors)
        raise ValueError(msg)

    return tests
