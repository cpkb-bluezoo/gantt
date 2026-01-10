#!/bin/bash
#
# Gantt vs Apache Ant Benchmark Suite
#
# This script runs comprehensive benchmarks comparing gantt with Apache Ant
# using real-world projects like gumdrop.
#

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GANTT_DIR="$(dirname "$SCRIPT_DIR")"
GANTT_BIN="${GANTT_DIR}/gantt"
GANTT_SCRIPTS="${GANTT_DIR}/bin"
RESULTS_DIR="${SCRIPT_DIR}/results"
GUMDROP_DIR="${HOME}/cpkb/gumdrop"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Number of iterations for each benchmark
ITERATIONS=${ITERATIONS:-5}

# Timestamp for this run
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Create results directory
mkdir -p "${RESULTS_DIR}"

#
# Utility Functions
#

print_header() {
    echo -e "\n${BOLD}${BLUE}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${BLUE}  $1${NC}"
    echo -e "${BOLD}${BLUE}═══════════════════════════════════════════════════════════════${NC}\n"
}

print_section() {
    echo -e "\n${CYAN}─── $1 ───${NC}\n"
}

print_info() {
    echo -e "${BLUE}ℹ${NC}  $1"
}

print_success() {
    echo -e "${GREEN}✓${NC}  $1"
}

print_warning() {
    echo -e "${YELLOW}⚠${NC}  $1"
}

print_error() {
    echo -e "${RED}✗${NC}  $1"
}

# High-resolution timing using perl (portable across macOS/Linux)
get_time_ns() {
    if command -v perl &>/dev/null; then
        perl -MTime::HiRes=time -e 'printf("%.9f\n", time())'
    else
        # Fallback to seconds
        date +%s.%N 2>/dev/null || date +%s
    fi
}

# Calculate elapsed time in milliseconds
elapsed_ms() {
    local start=$1
    local end=$2
    perl -e "printf('%.3f', ($end - $start) * 1000)" 2>/dev/null || echo "0"
}

# Run a command and measure time, capturing output
# Returns: time in milliseconds
time_command() {
    local label="$1"
    shift
    local cmd=("$@")
    
    local start end elapsed
    
    start=$(get_time_ns)
    "${cmd[@]}" > /dev/null 2>/tmp/gantt_benchmark_err.txt
    local exit_code=$?
    end=$(get_time_ns)
    
    elapsed=$(elapsed_ms "$start" "$end")
    
    if [ $exit_code -ne 0 ]; then
        echo "  Warning: $label failed (exit $exit_code)" >&2
        head -10 /tmp/gantt_benchmark_err.txt >&2
    fi
    
    echo "$elapsed"
}

# Run multiple iterations and compute statistics
# Sets global variables: BENCH_AVG, BENCH_MIN, BENCH_MAX
run_benchmark() {
    local name="$1"
    local iterations="$2"
    shift 2
    local cmd=("$@")
    
    local times=()
    local sum=0
    
    for i in $(seq 1 "$iterations"); do
        printf "  Run %d/%d: " "$i" "$iterations"
        local t
        t=$(time_command "run" "${cmd[@]}")
        times+=("$t")
        printf "%s ms\n" "$t"
        sum=$(perl -e "print $sum + $t")
    done
    
    # Calculate statistics
    BENCH_AVG=$(perl -e "printf('%.3f', $sum / $iterations)")
    
    # Sort times for min/max
    IFS=$'\n' sorted=($(sort -n <<<"${times[*]}"))
    unset IFS
    BENCH_MIN="${sorted[0]}"
    BENCH_MAX="${sorted[$((iterations-1))]}"
    
    echo ""
    echo "  +-----------------------------------"
    printf "  | Average: %s ms\n" "$BENCH_AVG"
    printf "  | Min:     %s ms\n" "$BENCH_MIN"
    printf "  | Max:     %s ms\n" "$BENCH_MAX"
    echo "  +-----------------------------------"
}

# Check prerequisites
check_prerequisites() {
    print_header "Checking Prerequisites"
    
    local all_ok=true
    
    # Check for gantt binary
    if [ -x "$GANTT_BIN" ]; then
        print_success "Gantt binary found: $GANTT_BIN"
    else
        print_warning "Gantt binary not found, attempting to build..."
        (cd "$GANTT_DIR" && make) || {
            print_error "Failed to build gantt"
            all_ok=false
        }
    fi
    
    # Check for ant
    if command -v ant &>/dev/null; then
        print_success "Apache Ant found: $(ant -version 2>&1 | head -1)"
    else
        print_error "Apache Ant not found in PATH"
        all_ok=false
    fi
    
    # Check for Java
    if command -v java &>/dev/null; then
        print_success "Java found: $(java -version 2>&1 | head -1)"
    else
        print_error "Java not found in PATH"
        all_ok=false
    fi
    
    # Check for gumdrop project
    if [ -f "$GUMDROP_DIR/build.xml" ]; then
        print_success "Gumdrop project found: $GUMDROP_DIR"
    else
        print_error "Gumdrop project not found at: $GUMDROP_DIR"
        all_ok=false
    fi
    
    # Check for perl (needed for timing)
    if command -v perl &>/dev/null; then
        print_success "Perl found (for high-resolution timing)"
    else
        print_warning "Perl not found, timing precision may be reduced"
    fi
    
    if [ "$all_ok" != true ]; then
        echo ""
        print_error "Some prerequisites are missing. Please fix before running benchmarks."
        exit 1
    fi
    
    echo ""
    print_success "All prerequisites satisfied!"
}

# Clean the gumdrop project
clean_gumdrop() {
    print_info "Cleaning gumdrop project..."
    (cd "$GUMDROP_DIR" && rm -rf build build-bootstrap dist test/junit/classes test/junit/results test/integration/classes test/integration/results manager/WEB-INF/classes test/cluster/WEB-INF/classes 2>/dev/null || true)
}

#
# Benchmark Functions
#

benchmark_startup() {
    print_header "Benchmark: Startup Time (No-op)"
    echo "Measures time to parse build.xml and evaluate properties without executing targets."
    echo ""
    
    # Create a minimal build.xml for pure startup measurement
    local tmpdir
    tmpdir=$(mktemp -d)
    cat > "$tmpdir/build.xml" << 'EOF'
<?xml version="1.0"?>
<project name="noop" default="noop">
    <property name="foo" value="bar"/>
    <target name="noop"/>
</project>
EOF
    
    print_section "Apache Ant"
    run_benchmark "ant-startup" "$ITERATIONS" ant -f "$tmpdir/build.xml" noop
    local ant_time="$BENCH_AVG"
    
    print_section "Gantt"
    PATH="$GANTT_SCRIPTS:$PATH" run_benchmark "gantt-startup" "$ITERATIONS" "$GANTT_BIN" -f "$tmpdir/build.xml" noop
    local gantt_time="$BENCH_AVG"
    
    rm -rf "$tmpdir"
    
    # Calculate speedup
    local speedup
    speedup=$(perl -e "printf('%.2fx', $ant_time / $gantt_time)")
    
    print_section "Summary"
    echo "  Apache Ant: $ant_time ms"
    echo "  Gantt:      $gantt_time ms"
    echo "  Speedup:    ${speedup} faster"
    
    echo "${TIMESTAMP},startup,ant,$ant_time" >> "$RESULTS_DIR/benchmark_log.csv"
    echo "${TIMESTAMP},startup,gantt,$gantt_time" >> "$RESULTS_DIR/benchmark_log.csv"
}

benchmark_gumdrop_clean() {
    print_header "Benchmark: Clean Build (gumdrop)"
    echo "Full clean build of gumdrop project (compile all Java sources, create JARs)."
    echo ""
    
    print_section "Apache Ant"
    clean_gumdrop
    run_benchmark "ant-clean-build" "$ITERATIONS" ant -f "$GUMDROP_DIR/build.xml" clean dist
    local ant_time="$BENCH_AVG"
    
    print_section "Gantt"
    clean_gumdrop
    PATH="$GANTT_SCRIPTS:$PATH" run_benchmark "gantt-clean-build" "$ITERATIONS" "$GANTT_BIN" -f "$GUMDROP_DIR/build.xml" clean dist
    local gantt_time="$BENCH_AVG"
    
    # Calculate speedup
    local speedup
    speedup=$(perl -e "printf('%.2fx', $ant_time / $gantt_time)")
    
    print_section "Summary"
    echo "  Apache Ant: $ant_time ms"
    echo "  Gantt:      $gantt_time ms"
    echo "  Speedup:    ${speedup} faster"
    
    echo "${TIMESTAMP},gumdrop_clean,ant,$ant_time" >> "$RESULTS_DIR/benchmark_log.csv"
    echo "${TIMESTAMP},gumdrop_clean,gantt,$gantt_time" >> "$RESULTS_DIR/benchmark_log.csv"
}

benchmark_gumdrop_incremental() {
    print_header "Benchmark: Incremental Build (gumdrop)"
    echo "Build with all sources already compiled (up-to-date check)."
    echo ""
    
    # First ensure project is built
    print_info "Preparing: building project first..."
    clean_gumdrop
    (cd "$GUMDROP_DIR" && ant dist > /dev/null 2>&1)
    
    print_section "Apache Ant"
    run_benchmark "ant-incremental" "$ITERATIONS" ant -f "$GUMDROP_DIR/build.xml" dist
    local ant_time="$BENCH_AVG"
    
    # Rebuild with gantt to ensure it has built classes
    clean_gumdrop
    (cd "$GUMDROP_DIR" && PATH="$GANTT_SCRIPTS:$PATH" "$GANTT_BIN" dist > /dev/null 2>&1)
    
    print_section "Gantt"
    PATH="$GANTT_SCRIPTS:$PATH" run_benchmark "gantt-incremental" "$ITERATIONS" "$GANTT_BIN" -f "$GUMDROP_DIR/build.xml" dist
    local gantt_time="$BENCH_AVG"
    
    # Calculate speedup
    local speedup
    speedup=$(perl -e "printf('%.2fx', $ant_time / $gantt_time)")
    
    print_section "Summary"
    echo "  Apache Ant: $ant_time ms"
    echo "  Gantt:      $gantt_time ms"
    echo "  Speedup:    ${speedup} faster"
    
    echo "${TIMESTAMP},gumdrop_incremental,ant,$ant_time" >> "$RESULTS_DIR/benchmark_log.csv"
    echo "${TIMESTAMP},gumdrop_incremental,gantt,$gantt_time" >> "$RESULTS_DIR/benchmark_log.csv"
}

benchmark_gumdrop_javadoc() {
    print_header "Benchmark: Javadoc Generation (gumdrop)"
    echo "Generate API documentation for gumdrop."
    echo ""
    
    print_section "Apache Ant"
    # Clean doc directory first
    rm -rf "$GUMDROP_DIR/doc"
    run_benchmark "ant-javadoc" 3 ant -f "$GUMDROP_DIR/build.xml" javadoc
    local ant_time="$BENCH_AVG"
    
    print_section "Gantt"
    rm -rf "$GUMDROP_DIR/doc"
    PATH="$GANTT_SCRIPTS:$PATH" run_benchmark "gantt-javadoc" 3 "$GANTT_BIN" -f "$GUMDROP_DIR/build.xml" javadoc
    local gantt_time="$BENCH_AVG"
    
    # Calculate speedup
    local speedup
    speedup=$(perl -e "printf('%.2fx', $ant_time / $gantt_time)")
    
    print_section "Summary"
    echo "  Apache Ant: $ant_time ms"
    echo "  Gantt:      $gantt_time ms"
    echo "  Speedup:    ${speedup} faster"
    
    echo "${TIMESTAMP},gumdrop_javadoc,ant,$ant_time" >> "$RESULTS_DIR/benchmark_log.csv"
    echo "${TIMESTAMP},gumdrop_javadoc,gantt,$gantt_time" >> "$RESULTS_DIR/benchmark_log.csv"
}

benchmark_property_expansion() {
    print_header "Benchmark: Property Expansion"
    echo "Measures property expansion performance with many properties and references."
    echo ""
    
    # Create a build.xml with lots of property expansions
    local tmpdir
    tmpdir=$(mktemp -d)
    cat > "$tmpdir/build.xml" << 'EOF'
<?xml version="1.0"?>
<project name="props" default="expand">
    <!-- Define 100 properties -->
EOF
    
    for i in $(seq 1 100); do
        echo "    <property name=\"prop$i\" value=\"value$i\"/>" >> "$tmpdir/build.xml"
    done
    
    # Add properties that reference other properties
    for i in $(seq 1 50); do
        echo "    <property name=\"ref$i\" value=\"\${prop$i}-\${prop$((i+50))}\"/>" >> "$tmpdir/build.xml"
    done
    
    cat >> "$tmpdir/build.xml" << 'EOF'
    
    <target name="expand">
        <echo message="${ref1}-${ref25}-${ref50}"/>
    </target>
</project>
EOF
    
    print_section "Apache Ant"
    run_benchmark "ant-props" "$ITERATIONS" ant -f "$tmpdir/build.xml" expand
    local ant_time="$BENCH_AVG"
    
    print_section "Gantt"
    PATH="$GANTT_SCRIPTS:$PATH" run_benchmark "gantt-props" "$ITERATIONS" "$GANTT_BIN" -f "$tmpdir/build.xml" expand
    local gantt_time="$BENCH_AVG"
    
    rm -rf "$tmpdir"
    
    # Calculate speedup
    local speedup
    speedup=$(perl -e "printf('%.2fx', $ant_time / $gantt_time)")
    
    print_section "Summary"
    echo "  Apache Ant: $ant_time ms"
    echo "  Gantt:      $gantt_time ms"
    echo "  Speedup:    ${speedup} faster"
    
    echo "${TIMESTAMP},property_expansion,ant,$ant_time" >> "$RESULTS_DIR/benchmark_log.csv"
    echo "${TIMESTAMP},property_expansion,gantt,$gantt_time" >> "$RESULTS_DIR/benchmark_log.csv"
}

benchmark_file_operations() {
    print_header "Benchmark: File Operations"
    echo "Measures mkdir, copy, delete operations."
    echo ""
    
    # Create a build.xml with file operations
    local tmpdir
    tmpdir=$(mktemp -d)
    mkdir -p "$tmpdir/src"
    
    # Create some source files
    for i in $(seq 1 50); do
        echo "File $i content" > "$tmpdir/src/file$i.txt"
    done
    
    cat > "$tmpdir/build.xml" << EOF
<?xml version="1.0"?>
<project name="fileops" default="fileops" basedir="$tmpdir">
    <target name="fileops">
        <mkdir dir="build"/>
        <copy todir="build">
            <fileset dir="src" includes="*.txt"/>
        </copy>
        <delete dir="build"/>
    </target>
</project>
EOF
    
    print_section "Apache Ant"
    run_benchmark "ant-fileops" "$ITERATIONS" ant -f "$tmpdir/build.xml" fileops
    local ant_time="$BENCH_AVG"
    
    print_section "Gantt"
    PATH="$GANTT_SCRIPTS:$PATH" run_benchmark "gantt-fileops" "$ITERATIONS" "$GANTT_BIN" -f "$tmpdir/build.xml" fileops
    local gantt_time="$BENCH_AVG"
    
    rm -rf "$tmpdir"
    
    # Calculate speedup
    local speedup
    speedup=$(perl -e "printf('%.2fx', $ant_time / $gantt_time)")
    
    print_section "Summary"
    echo "  Apache Ant: $ant_time ms"
    echo "  Gantt:      $gantt_time ms"
    echo "  Speedup:    ${speedup} faster"
    
    echo "${TIMESTAMP},file_operations,ant,$ant_time" >> "$RESULTS_DIR/benchmark_log.csv"
    echo "${TIMESTAMP},file_operations,gantt,$gantt_time" >> "$RESULTS_DIR/benchmark_log.csv"
}

# Profile gantt execution using dtrace/dtruss (macOS) or strace (Linux)
profile_gantt() {
    print_header "Profiling Gantt Execution"
    echo "Detailed timing breakdown of gantt execution phases."
    echo ""
    
    if [ "$(uname)" = "Darwin" ]; then
        print_info "Using DTrace for profiling (may require sudo)..."
        # Simple time profiling - count syscalls
        print_warning "Full DTrace profiling requires root. Showing basic timing instead."
    fi
    
    # Use GANTT_VERBOSE if available
    print_section "Verbose Build Output"
    clean_gumdrop
    print_info "Running gantt with verbose output..."
    
    local start end elapsed
    start=$(get_time_ns)
    PATH="$GANTT_SCRIPTS:$PATH" "$GANTT_BIN" -verbose -f "$GUMDROP_DIR/build.xml" dist 2>&1 | head -50
    end=$(get_time_ns)
    elapsed=$(elapsed_ms "$start" "$end")
    
    echo ""
    echo "Total time: $elapsed ms"
}

generate_report() {
    print_header "Benchmark Report"
    
    local report_file="${RESULTS_DIR}/report_${TIMESTAMP}.txt"
    
    {
        echo "Gantt vs Apache Ant Benchmark Report"
        echo "====================================="
        echo ""
        echo "Date: $(date)"
        echo "System: $(uname -a)"
        echo "Java: $(java -version 2>&1 | head -1)"
        echo "Ant: $(ant -version 2>&1 | head -1)"
        echo "Gantt: $($GANTT_BIN -version 2>&1)"
        echo ""
        echo "Iterations per benchmark: $ITERATIONS"
        echo ""
        echo "Results (times in milliseconds):"
        echo "--------------------------------"
        cat "$RESULTS_DIR/benchmark_log.csv" | grep "^${TIMESTAMP}," | \
            awk -F, '{printf "%-25s %-10s %s ms\n", $2, $3, $4}'
    } | tee "$report_file"
    
    echo ""
    print_success "Report saved to: $report_file"
}

# Main
main() {
    print_header "Gantt vs Apache Ant Benchmark Suite"
    echo "Comparing build system performance with real-world projects"
    echo ""
    
    check_prerequisites
    
    # Initialize log file
    if [ ! -f "$RESULTS_DIR/benchmark_log.csv" ]; then
        echo "timestamp,benchmark,tool,time_ms" > "$RESULTS_DIR/benchmark_log.csv"
    fi
    
    # Run benchmarks
    benchmark_startup
    benchmark_property_expansion
    benchmark_file_operations
    benchmark_gumdrop_clean
    benchmark_gumdrop_incremental
    benchmark_gumdrop_javadoc
    
    # Generate report
    generate_report
    
    print_header "Benchmark Complete!"
    print_info "Results stored in: $RESULTS_DIR"
}

# Handle command line arguments
case "${1:-all}" in
    startup)
        check_prerequisites
        benchmark_startup
        ;;
    clean)
        check_prerequisites
        benchmark_gumdrop_clean
        ;;
    incremental)
        check_prerequisites
        benchmark_gumdrop_incremental
        ;;
    javadoc)
        check_prerequisites
        benchmark_gumdrop_javadoc
        ;;
    props|properties)
        check_prerequisites
        benchmark_property_expansion
        ;;
    fileops|files)
        check_prerequisites
        benchmark_file_operations
        ;;
    profile)
        check_prerequisites
        profile_gantt
        ;;
    all)
        main
        ;;
    *)
        echo "Usage: $0 [benchmark]"
        echo ""
        echo "Benchmarks:"
        echo "  startup      - Measure startup/parsing time"
        echo "  clean        - Full clean build of gumdrop"
        echo "  incremental  - Incremental build (no changes)"
        echo "  javadoc      - Javadoc generation"
        echo "  props        - Property expansion"
        echo "  fileops      - File operations"
        echo "  profile      - Profile gantt execution"
        echo "  all          - Run all benchmarks (default)"
        echo ""
        echo "Environment variables:"
        echo "  ITERATIONS   - Number of iterations per benchmark (default: 5)"
        exit 1
        ;;
esac

