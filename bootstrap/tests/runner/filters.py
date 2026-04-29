"""Test filtering by name, tier, tags, and category."""

from __future__ import annotations

import fnmatch

from .core import TestCase


def filter_tests(
    tests: list[TestCase],
    name: str | None = None,
    tier: int | None = None,
    tags: list[str] | None = None,
    exclude_tags: list[str] | None = None,
    category: str | None = None,
) -> list[TestCase]:
    """Filter test cases by the given criteria. All criteria are ANDed together."""
    result = tests

    if name is not None:
        result = [t for t in result if fnmatch.fnmatch(t.metadata.name, name)]

    if tier is not None:
        result = [t for t in result if t.metadata.tier == tier]

    if tags is not None:
        tag_set = set(tags)
        result = [t for t in result if tag_set & set(t.metadata.tags)]

    if exclude_tags is not None:
        excl_set = set(exclude_tags)
        result = [t for t in result if not (excl_set & set(t.metadata.tags))]

    if category is not None:
        result = [t for t in result if t.category == category]

    return result
