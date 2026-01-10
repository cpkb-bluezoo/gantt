#!/bin/sh
# Gantt Test Runner
#
# Runs all test projects and reports results.
# Each test directory should contain a build.xml and optionally
# a verify.sh script to check expected outputs.

set -e

# Get the directory containing this script
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Set up PATH to include gantt executable and scripts
export PATH="$PROJECT_ROOT:$PROJECT_ROOT/bin:$PATH"

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
if [ ! -x "$PROJECT_ROOT/gantt" ]; then
    echo "${RED}Error: gantt executable not found. Run 'make' first.${NC}"
    exit 1
fi

echo "========================================"
echo "Gantt Test Suite"
echo "========================================"
echo "Project root: $PROJECT_ROOT"
echo "PATH includes: $PROJECT_ROOT and $PROJECT_ROOT/bin"
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
    
    cd "$SCRIPT_DIR"
}

# Find and run all tests
# Tests are directories starting with digits (for ordering)
for test_dir in "$SCRIPT_DIR"/[0-9]*; do
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


