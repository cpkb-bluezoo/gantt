#!/bin/sh
# Copyright (C) 2026 Chris Burdess <dog@gnu.org>
# SPDX-License-Identifier: GPL-3.0-or-later
# Gantt Test Runner
#
# Runs all test projects and reports results.
# Each test directory should contain a build.xml and optionally
# a verify.sh script to check expected outputs.

set -e

# Get the directory containing this script
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Where the built gantt and helper binaries live.  'make check' sets
# GANTT_BUILDDIR; run directly, it is the source tree (in-tree build).
BUILD_DIR="${GANTT_BUILDDIR:-$PROJECT_ROOT}"

# Set up PATH to include gantt executable and scripts
export PATH="$BUILD_DIR:$PROJECT_ROOT/bin:$PATH"

# Tests write their output next to their build.xml, so run them from a copy
# in the build tree.  This keeps the source tree clean (and read-only for
# 'make distcheck').
WORK_DIR="$BUILD_DIR/test-work"
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
for d in "$SCRIPT_DIR"/[0-9]*; do
    if [ -d "$d" ]; then
        cp -Rp "$d" "$WORK_DIR/"
        chmod -R u+w "$WORK_DIR/$(basename "$d")"
    fi
done

# Colors for output (if terminal supports it)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    NC='\033[0m' # No Color
else
    RED=''
    GREEN=''
    YELLOW=''
    NC=''
fi

# Counters
PASSED=0
FAILED=0
SKIPPED=0

# Check gantt is built
if [ ! -x "$BUILD_DIR/gantt" ]; then
    echo "${RED}Error: gantt executable not found. Run 'make' first.${NC}"
    exit 1
fi

echo "========================================"
echo "Gantt Test Suite"
echo "========================================"
echo "Project root: $PROJECT_ROOT"
echo "PATH includes: $BUILD_DIR and $PROJECT_ROOT/bin"
echo "Work directory: $WORK_DIR"
echo ""

# Run a single test
run_test() {
    test_dir="$1"
    test_name=$(basename "$test_dir")
    
    echo "----------------------------------------"
    echo "Running: $test_name"
    echo "----------------------------------------"
    
    # Check for build.xml
    if [ ! -f "$test_dir/build.xml" ]; then
        echo "${YELLOW}SKIPPED${NC} - no build.xml"
        SKIPPED=$((SKIPPED + 1))
        return
    fi
    
    # Clean any previous test output
    if [ -d "$test_dir/build" ]; then
        rm -rf "$test_dir/build"
    fi
    if [ -d "$test_dir/dist" ]; then
        rm -rf "$test_dir/dist"
    fi
    
    # Run gantt
    cd "$test_dir"
    
    if gantt 2>&1; then
        # Check if there's a verification script
        if [ -x "$test_dir/verify.sh" ]; then
            if "$test_dir/verify.sh"; then
                echo "${GREEN}PASSED${NC}"
                PASSED=$((PASSED + 1))
            else
                echo "${RED}FAILED${NC} - verification failed"
                FAILED=$((FAILED + 1))
            fi
        else
            echo "${GREEN}PASSED${NC}"
            PASSED=$((PASSED + 1))
        fi
    else
        echo "${RED}FAILED${NC} - gantt returned error"
        FAILED=$((FAILED + 1))
    fi
    
    cd "$WORK_DIR"
}

# Find and run all tests
# Tests are directories starting with digits (for ordering)
for test_dir in "$WORK_DIR"/[0-9]*; do
    if [ -d "$test_dir" ]; then
        run_test "$test_dir"
    fi
done

# Summary
echo ""
echo "========================================"
echo "Test Results"
echo "========================================"
echo "${GREEN}Passed:${NC}  $PASSED"
echo "${RED}Failed:${NC}  $FAILED"
echo "${YELLOW}Skipped:${NC} $SKIPPED"
echo ""

if [ $FAILED -gt 0 ]; then
    echo "${RED}SOME TESTS FAILED${NC}"
    exit 1
else
    echo "${GREEN}ALL TESTS PASSED${NC}"
    exit 0
fi


