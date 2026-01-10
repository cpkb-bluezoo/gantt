#!/bin/bash
#
# Detailed Build Profiling
#
# This script provides detailed timing breakdowns for individual build phases.
# It helps identify bottlenecks in both gantt and Apache Ant.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GANTT_DIR="$(dirname "$SCRIPT_DIR")"
GANTT_BIN="${GANTT_DIR}/gantt"
GANTT_SCRIPTS="${GANTT_DIR}/bin"
GUMDROP_DIR="${HOME}/cpkb/gumdrop"
RESULTS_DIR="${SCRIPT_DIR}/results"

# Colors
BOLD='\033[1m'
CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

mkdir -p "$RESULTS_DIR"

get_time_ns() {
    perl -MTime::HiRes=time -e 'printf("%.9f\n", time())'
}

elapsed_ms() {
    perl -e "printf('%.3f', ($2 - $1) * 1000)"
}

print_bar() {
    local value=$1
    local max=$2
    local width=40
    local filled
    filled=$(perl -e "print int($value / $max * $width)")
    local empty=$((width - filled))
    printf "["
    for ((i=0; i<filled; i++)); do printf "█"; done
    for ((i=0; i<empty; i++)); do printf "░"; done
    printf "]"
}

#
# Profile individual targets in gumdrop
#
profile_targets() {
    local tool="$1"
    local label="$2"
    
    echo -e "\n${BOLD}${CYAN}Profiling: $label${NC}\n"
    
    # Clean first
    rm -rf "$GUMDROP_DIR/build" "$GUMDROP_DIR/build-bootstrap" "$GUMDROP_DIR/dist" 2>/dev/null || true
    
    local targets=("build" "jar" "build-bootstrap" "bootstrap-jar" "manager-war")
    local times=()
    local max_time=0
    
    for target in "${targets[@]}"; do
        local start end elapsed
        start=$(get_time_ns)
        
        if [ "$tool" = "ant" ]; then
            ant -f "$GUMDROP_DIR/build.xml" "$target" > /dev/null 2>&1
        else
            PATH="$GANTT_SCRIPTS:$PATH" "$GANTT_BIN" -f "$GUMDROP_DIR/build.xml" "$target" > /dev/null 2>&1
        fi
        
        end=$(get_time_ns)
        elapsed=$(elapsed_ms "$start" "$end")
        times+=("$elapsed")
        
        # Track max for bar chart scaling
        if perl -e "exit($elapsed > $max_time ? 0 : 1)"; then
            max_time="$elapsed"
        fi
    done
    
    # Print results with bar chart
    echo "Target                Time (ms)    Distribution"
    echo "──────────────────────────────────────────────────────────────────"
    
    for i in "${!targets[@]}"; do
        local t="${times[$i]}"
        printf "%-20s  %8s    " "${targets[$i]}" "$t"
        print_bar "$t" "$max_time"
        echo ""
    done
    
    # Calculate total
    local total=0
    for t in "${times[@]}"; do
        total=$(perl -e "print $total + $t")
    done
    
    echo "──────────────────────────────────────────────────────────────────"
    printf "%-20s  ${BOLD}%8s${NC}\n" "TOTAL" "$total"
    
    echo "$total"
}

#
# Compare specific phases
#
compare_phases() {
    echo -e "\n${BOLD}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}  Phase-by-Phase Comparison${NC}"
    echo -e "${BOLD}═══════════════════════════════════════════════════════════════${NC}"
    
    local ant_total gantt_total
    
    ant_total=$(profile_targets "ant" "Apache Ant")
    gantt_total=$(profile_targets "gantt" "Gantt")
    
    echo -e "\n${BOLD}Summary:${NC}"
    local speedup
    speedup=$(perl -e "printf('%.2fx', $ant_total / $gantt_total)")
    printf "  Apache Ant total: ${YELLOW}%s ms${NC}\n" "$ant_total"
    printf "  Gantt total:      ${GREEN}%s ms${NC}\n" "$gantt_total"
    printf "  Speedup:          ${BOLD}${GREEN}%s${NC}\n" "$speedup"
}

#
# Measure XML parsing overhead
#
measure_xml_parsing() {
    echo -e "\n${BOLD}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}  XML Parsing Overhead${NC}"
    echo -e "${BOLD}═══════════════════════════════════════════════════════════════${NC}\n"
    
    # Create increasingly complex build files
    local tmpdir
    tmpdir=$(mktemp -d)
    
    echo "Testing parsing time with increasing file complexity..."
    echo ""
    echo "Targets    Ant (ms)    Gantt (ms)    Speedup"
    echo "─────────────────────────────────────────────"
    
    for num_targets in 10 50 100 200; do
        # Generate build file
        cat > "$tmpdir/build.xml" << 'EOF'
<?xml version="1.0"?>
<project name="test" default="target1">
EOF
        
        for i in $(seq 1 $num_targets); do
            cat >> "$tmpdir/build.xml" << EOF
    <target name="target$i">
        <property name="p$i" value="v$i"/>
    </target>
EOF
        done
        
        echo "</project>" >> "$tmpdir/build.xml"
        
        # Time Ant
        local start end ant_time gantt_time
        start=$(get_time_ns)
        ant -f "$tmpdir/build.xml" target1 > /dev/null 2>&1
        end=$(get_time_ns)
        ant_time=$(elapsed_ms "$start" "$end")
        
        # Time Gantt
        start=$(get_time_ns)
        PATH="$GANTT_SCRIPTS:$PATH" "$GANTT_BIN" -f "$tmpdir/build.xml" target1 > /dev/null 2>&1
        end=$(get_time_ns)
        gantt_time=$(elapsed_ms "$start" "$end")
        
        local speedup
        speedup=$(perl -e "printf('%.2fx', $ant_time / $gantt_time)")
        
        printf "%-10s  %8s    %10s    %s\n" "$num_targets" "$ant_time" "$gantt_time" "$speedup"
    done
    
    rm -rf "$tmpdir"
}

#
# Measure javac performance
#
measure_javac() {
    echo -e "\n${BOLD}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}  Java Compilation Time Breakdown${NC}"
    echo -e "${BOLD}═══════════════════════════════════════════════════════════════${NC}\n"
    
    # Clean
    rm -rf "$GUMDROP_DIR/build" 2>/dev/null || true
    mkdir -p "$GUMDROP_DIR/build"
    
    # Count source files
    local src_count
    src_count=$(find "$GUMDROP_DIR/src" -name "*.java" | wc -l | tr -d ' ')
    echo "Source files: $src_count"
    echo ""
    
    # Time just the javac portion (ant build target)
    echo "Measuring compilation time..."
    
    local start end
    
    # Ant
    rm -rf "$GUMDROP_DIR/build"
    start=$(get_time_ns)
    ant -f "$GUMDROP_DIR/build.xml" build > /dev/null 2>&1
    end=$(get_time_ns)
    local ant_time
    ant_time=$(elapsed_ms "$start" "$end")
    
    # Gantt
    rm -rf "$GUMDROP_DIR/build"
    start=$(get_time_ns)
    PATH="$GANTT_SCRIPTS:$PATH" "$GANTT_BIN" -f "$GUMDROP_DIR/build.xml" build > /dev/null 2>&1
    end=$(get_time_ns)
    local gantt_time
    gantt_time=$(elapsed_ms "$start" "$end")
    
    echo "Results for 'build' target (javac + copy):"
    printf "  Apache Ant: ${YELLOW}%s ms${NC}\n" "$ant_time"
    printf "  Gantt:      ${GREEN}%s ms${NC}\n" "$gantt_time"
    
    local speedup
    speedup=$(perl -e "printf('%.2fx', $ant_time / $gantt_time)")
    printf "  Speedup:    ${BOLD}%s${NC}\n" "$speedup"
    
    # Files per second
    local ant_fps gantt_fps
    ant_fps=$(perl -e "printf('%.1f', $src_count / ($ant_time / 1000))")
    gantt_fps=$(perl -e "printf('%.1f', $src_count / ($gantt_time / 1000))")
    
    echo ""
    echo "Compilation throughput:"
    printf "  Ant:   %s files/sec\n" "$ant_fps"
    printf "  Gantt: %s files/sec\n" "$gantt_fps"
}

#
# Memory usage comparison (macOS specific)
#
measure_memory() {
    echo -e "\n${BOLD}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}  Memory Usage Comparison${NC}"
    echo -e "${BOLD}═══════════════════════════════════════════════════════════════${NC}\n"
    
    if [ "$(uname)" != "Darwin" ]; then
        echo "Memory profiling currently only supported on macOS"
        return
    fi
    
    # Clean
    rm -rf "$GUMDROP_DIR/build" "$GUMDROP_DIR/build-bootstrap" "$GUMDROP_DIR/dist" 2>/dev/null || true
    
    echo "Measuring peak memory usage..."
    echo "(Using /usr/bin/time for resident set size)"
    echo ""
    
    # Ant (we can't easily measure JVM memory this way, but we can try)
    echo "Apache Ant:"
    /usr/bin/time -l ant -f "$GUMDROP_DIR/build.xml" dist 2>&1 | grep -E "(real|maximum resident)" | head -5
    
    echo ""
    
    # Clean again
    rm -rf "$GUMDROP_DIR/build" "$GUMDROP_DIR/build-bootstrap" "$GUMDROP_DIR/dist" 2>/dev/null || true
    
    # Gantt
    echo "Gantt:"
    PATH="$GANTT_SCRIPTS:$PATH" /usr/bin/time -l "$GANTT_BIN" -f "$GUMDROP_DIR/build.xml" dist 2>&1 | grep -E "(real|maximum resident)" | head -5
    
    echo ""
    echo "Note: Gantt's memory footprint is much smaller because it doesn't include"
    echo "the JVM overhead. The actual Java compilation still spawns javac which uses"
    echo "its own JVM process."
}

#
# Main
#
main() {
    echo -e "${BOLD}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}  Gantt Build Profiler${NC}"
    echo -e "${BOLD}═══════════════════════════════════════════════════════════════${NC}"
    
    # Check prerequisites
    if [ ! -x "$GANTT_BIN" ]; then
        echo "Building gantt..."
        (cd "$GANTT_DIR" && make)
    fi
    
    if ! command -v ant &>/dev/null; then
        echo "Error: Apache Ant not found"
        exit 1
    fi
    
    case "${1:-all}" in
        phases)
            compare_phases
            ;;
        xml)
            measure_xml_parsing
            ;;
        javac)
            measure_javac
            ;;
        memory)
            measure_memory
            ;;
        all)
            measure_xml_parsing
            measure_javac
            compare_phases
            measure_memory
            ;;
        *)
            echo "Usage: $0 [phases|xml|javac|memory|all]"
            exit 1
            ;;
    esac
}

main "$@"

