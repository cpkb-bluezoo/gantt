#!/bin/bash
#
# Quick single-run comparison between gantt and Apache Ant
#
# Usage: ./quick-compare.sh [target]
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GANTT_DIR="$(dirname "$SCRIPT_DIR")"
GANTT_BIN="${GANTT_DIR}/gantt"
GANTT_SCRIPTS="${GANTT_DIR}/bin"
GUMDROP_DIR="${HOME}/cpkb/gumdrop"

# Target to run (default: dist)
TARGET="${1:-dist}"

# Colors
BOLD='\033[1m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

get_time_ns() {
    perl -MTime::HiRes=time -e 'printf("%.9f\n", time())'
}

elapsed_ms() {
    perl -e "printf('%.3f', ($2 - $1) * 1000)"
}

# Check prerequisites
if [ ! -x "$GANTT_BIN" ]; then
    echo "Building gantt..."
    (cd "$GANTT_DIR" && make)
fi

if ! command -v ant &>/dev/null; then
    echo "Error: Apache Ant not found in PATH"
    exit 1
fi

echo -e "${BOLD}${CYAN}Quick Compare: gantt vs ant${NC}"
echo "Target: $TARGET"
echo "Project: gumdrop"
echo ""

# Clean if dist target
if [ "$TARGET" = "dist" ] || [ "$TARGET" = "clean" ]; then
    echo "Cleaning project..."
    rm -rf "$GUMDROP_DIR/build" "$GUMDROP_DIR/build-bootstrap" "$GUMDROP_DIR/dist" 2>/dev/null || true
fi

# Run with Ant
echo -e "\n${YELLOW}Running Apache Ant...${NC}"
start=$(get_time_ns)
ant -f "$GUMDROP_DIR/build.xml" "$TARGET"
end=$(get_time_ns)
ant_time=$(elapsed_ms "$start" "$end")

# Clean again
if [ "$TARGET" = "dist" ]; then
    rm -rf "$GUMDROP_DIR/build" "$GUMDROP_DIR/build-bootstrap" "$GUMDROP_DIR/dist" 2>/dev/null || true
fi

# Run with Gantt
echo -e "\n${GREEN}Running Gantt...${NC}"
start=$(get_time_ns)
PATH="$GANTT_SCRIPTS:$PATH" "$GANTT_BIN" -f "$GUMDROP_DIR/build.xml" "$TARGET"
end=$(get_time_ns)
gantt_time=$(elapsed_ms "$start" "$end")

# Results
speedup=$(perl -e "printf('%.2f', $ant_time / $gantt_time)")

echo ""
echo -e "${BOLD}═══════════════════════════════════════${NC}"
echo -e "${BOLD}Results${NC}"
echo -e "${BOLD}═══════════════════════════════════════${NC}"
printf "Apache Ant:  ${YELLOW}%10s ms${NC}\n" "$ant_time"
printf "Gantt:       ${GREEN}%10s ms${NC}\n" "$gantt_time"
printf "Speedup:     ${BOLD}%10sx${NC}\n" "$speedup"
echo -e "${BOLD}═══════════════════════════════════════${NC}"

