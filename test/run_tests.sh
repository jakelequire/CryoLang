#!/bin/bash
#*******************************************************************************
#  Copyright 2025 Jacob LeQuire                                                *
#  SPDX-License-Identifier: Apache-2.0                                         *
#*******************************************************************************

#*******************************************************************************
# CryoLang Test Runner Script                                                  *
#*******************************************************************************

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Default values
TEST_DIR="test/test-cases"
COMPILER="bin/cryo"
VERBOSE=false
FAIL_FAST=false
SHOW_FAILURES=false

# Function to show usage
show_usage() {
    echo -e "${BOLD}CryoLang Test Runner${NC}"
    echo "Usage: $0 [OPTIONS] [COMMAND]"
    echo ""
    echo -e "${BOLD}Commands:${NC}"
    echo "  all                 Run all tests (default)"
    echo "  quick               Run tests with fail-fast mode"
    echo "  verbose             Run tests with verbose output"
    echo "  category CATS...    Run specific categories"
    echo "  file FILE          Run a specific test file"
    echo "  list               List all available test files"
    echo ""
    echo -e "${BOLD}Options:${NC}"
    echo "  -h, --help         Show this help message"
    echo "  -v, --verbose      Enable verbose output"
    echo "  -f, --fail-fast    Stop on first failure"
    echo "  -s, --show-failures Show detailed failure information"
    echo "  --test-dir DIR     Test directory (default: test/test-cases)"
    echo "  --compiler PATH    Compiler path (default: bin/cryo)"
    echo ""
    echo -e "${BOLD}Examples:${NC}"
    echo "  $0                                    # Run all tests"
    echo "  $0 quick                              # Run with fail-fast"
    echo "  $0 verbose                            # Run with verbose output"
    echo "  $0 category functions control-flow    # Run specific categories"
    echo "  $0 file test/test-cases/functions/basic_functions.cryo"
    echo "  $0 list                               # List all test files"
}

# Function to check if compiler exists
check_compiler() {
    if [ ! -f "$COMPILER" ]; then
        echo -e "${RED}Error: Compiler not found at '$COMPILER'${NC}"
        echo -e "${YELLOW}Hint: Run 'make all' to build the compiler first${NC}"
        exit 1
    fi
}

# Function to list all test files
list_tests() {
    if [ ! -d "$TEST_DIR" ]; then
        echo -e "${RED}Error: Test directory '$TEST_DIR' does not exist${NC}"
        exit 1
    fi
    
    echo -e "${CYAN}Available test files:${NC}"
    find "$TEST_DIR" -name "*.cryo" | sort | while read -r file; do
        relative_path="${file#$TEST_DIR/}"
        echo "  $relative_path"
    done
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_usage
            exit 0
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -f|--fail-fast)
            FAIL_FAST=true
            shift
            ;;
        -s|--show-failures)
            SHOW_FAILURES=true
            shift
            ;;
        --test-dir)
            TEST_DIR="$2"
            shift 2
            ;;
        --compiler)
            COMPILER="$2"
            shift 2
            ;;
        all)
            COMMAND="all"
            shift
            ;;
        quick)
            COMMAND="quick"
            FAIL_FAST=true
            shift
            ;;
        verbose)
            COMMAND="verbose"
            VERBOSE=true
            SHOW_FAILURES=true
            shift
            ;;
        category)
            COMMAND="category"
            shift
            CATEGORIES=()
            while [[ $# -gt 0 && ! $1 =~ ^- ]]; do
                CATEGORIES+=("$1")
                shift
            done
            ;;
        file)
            COMMAND="file"
            FILE="$2"
            shift 2
            ;;
        list)
            list_tests
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            show_usage
            exit 1
            ;;
    esac
done

# Default command
if [ -z "$COMMAND" ]; then
    COMMAND="all"
fi

# Build test runner arguments
ARGS=("--test-dir" "$TEST_DIR" "--compiler" "$COMPILER")

if [ "$VERBOSE" = true ]; then
    ARGS+=("--verbose")
fi

if [ "$FAIL_FAST" = true ]; then
    ARGS+=("--fail-fast")
fi

if [ "$SHOW_FAILURES" = true ]; then
    ARGS+=("--show-failures")
fi

# Execute based on command
case $COMMAND in
    all|quick|verbose)
        check_compiler
        echo -e "${BOLD}${CYAN}Running CryoLang test suite...${NC}"
        python3 test/test_runner.py "${ARGS[@]}"
        ;;
    category)
        if [ ${#CATEGORIES[@]} -eq 0 ]; then
            echo -e "${RED}Error: No categories specified${NC}"
            echo -e "${YELLOW}Available categories: functions variables control-flow data-types generics memory${NC}"
            exit 1
        fi
        check_compiler
        echo -e "${BOLD}${CYAN}Running tests for categories: ${CATEGORIES[*]}${NC}"
        python3 test/test_runner.py "${ARGS[@]}" --categories "${CATEGORIES[@]}"
        ;;
    file)
        if [ -z "$FILE" ]; then
            echo -e "${RED}Error: No file specified${NC}"
            exit 1
        fi
        if [ ! -f "$FILE" ]; then
            echo -e "${RED}Error: Test file '$FILE' does not exist${NC}"
            exit 1
        fi
        check_compiler
        echo -e "${BOLD}${CYAN}Running test file: $FILE${NC}"
        python3 test/test_runner.py "${ARGS[@]}" --file "$FILE"
        ;;
esac