# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project will adhere to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
once it reaches v0.1.0.

## [Unreleased]

The 0.1.0 release line — establishing the baseline for users and CI.

### Added
- Top-level `Makefile` orchestrating bootstrap, stdlib, cryo, and selfhost-check.
- `make selfhost-check` builds 8 stages and verifies stage-4 / stage-5 IR
  byte-identity (the project's gold-standard regression gate).
- PATH-wrapper installer (`install.sh`) for in-place install of the
  self-hosted compiler.
- `LICENSE` (Apache-2.0), `CONTRIBUTING.md`, GitHub issue & PR templates,
  and CI workflow.

### Changed
- Repository layout reorganized:
  - `bootstrap/` → `legacy/bootstrap/` (frozen C++ implementation)
  - `cryoc/` → `compiler/` (self-hosted Cryo compiler)
  - `new_stdlib/` → `experimental/stdlib-next/` (parked rewrite)
- Self-hosted compiler binary renamed `cryoc` → `cryo`.

### Fixed
- `legacy/bootstrap/`: silenced stale `stdlib/` and `bin/` probe warnings
  that surfaced after the directory move.
