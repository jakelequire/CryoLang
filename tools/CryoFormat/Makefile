# Makefile for CryoFormat

.PHONY: all build test clean release install check fmt clippy help

# Default target
all: build test

# Build the project
build:
	cargo build

# Build in release mode
release:
	cargo build --release

# Run tests
test:
	cargo test

# Run integration tests
test-integration:
	cargo test --test integration_tests

# Clean build artifacts
clean:
	cargo clean

# Install the binary
install:
	cargo install --path .

# Check formatting
check:
	cargo fmt --check

# Format code
fmt:
	cargo fmt

# Run clippy linter
clippy:
	cargo clippy -- -D warnings

# Run all quality checks
check-all: fmt clippy test

# Development workflow
dev: build test clippy

# CI workflow
ci: check-all test-integration

# Show available targets
help:
	@echo "Available targets:"
	@echo "  build          - Build the project"
	@echo "  release        - Build in release mode"
	@echo "  test           - Run unit tests"
	@echo "  test-integration - Run integration tests"
	@echo "  clean          - Clean build artifacts"
	@echo "  install        - Install the binary"
	@echo "  check          - Check code formatting"
	@echo "  fmt            - Format code"
	@echo "  clippy         - Run clippy linter"
	@echo "  check-all      - Run all quality checks"
	@echo "  dev            - Development workflow (build + test + clippy)"
	@echo "  ci             - CI workflow (all checks + integration tests)"
	@echo "  help           - Show this help message"