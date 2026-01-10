# Gantt Benchmarks

This directory contains benchmarking tools for comparing gantt with Apache Ant.

## Quick Start

```bash
# Make scripts executable
chmod +x *.sh

# Run all benchmarks
./run-benchmarks.sh

# Run a specific benchmark
./run-benchmarks.sh startup
./run-benchmarks.sh clean
./run-benchmarks.sh incremental
```

## Benchmark Scripts

### `run-benchmarks.sh`

The main benchmark runner. Compares gantt and Apache Ant across multiple scenarios:

| Benchmark | Description |
|-----------|-------------|
| `startup` | Measures XML parsing and property evaluation without executing targets |
| `clean` | Full clean build of gumdrop (compile, jar, copy) |
| `incremental` | Build when all sources are already compiled (up-to-date check) |
| `javadoc` | Javadoc generation for gumdrop |
| `props` | Property expansion with 100+ properties |
| `fileops` | File operations (mkdir, copy, delete) |

#### Usage

```bash
# Run all benchmarks (default)
./run-benchmarks.sh

# Run specific benchmark
./run-benchmarks.sh startup

# Customize iterations (default: 5)
ITERATIONS=10 ./run-benchmarks.sh clean
```

### `profile-build.sh`

Detailed profiling of build phases. Helps identify specific bottlenecks.

```bash
# Profile all aspects
./profile-build.sh

# Profile specific aspects
./profile-build.sh phases    # Phase-by-phase comparison
./profile-build.sh xml       # XML parsing overhead
./profile-build.sh javac     # Java compilation time
./profile-build.sh memory    # Memory usage (macOS)
```

### `quick-compare.sh`

Single-run comparison for quick feedback during development.

```bash
./quick-compare.sh          # Default: build gumdrop 'dist' target
./quick-compare.sh clean    # Run 'clean' target
./quick-compare.sh javadoc  # Run 'javadoc' target
```

## Test Project

The benchmarks use **gumdrop** (`~/cpkb/gumdrop`) as the test project. Gumdrop is a substantial Java application with:

- ~200+ Java source files
- Multiple compilation targets
- JAR and WAR packaging
- Javadoc generation
- Unit and integration tests

This provides realistic timing data for a medium-to-large Java project.

## Results

Benchmark results are stored in `results/`:

- `benchmark_log.csv` - Raw timing data (appended each run)
- `report_YYYYMMDD_HHMMSS.txt` - Human-readable reports

### Sample CSV Format

```csv
timestamp,benchmark,tool,time_ms
20260102_143022,startup,ant,1234.567
20260102_143022,startup,gantt,45.678
```

## What Gets Measured

### JVM Startup vs Native Binary

The most significant performance difference is expected in **startup time**:

- **Apache Ant**: JVM startup (~500-1500ms) + Ant initialization
- **Gantt**: Native binary startup (~10-50ms)

For incremental builds (where actual work is minimal), this overhead dominates.

### XML Parsing

- **Ant**: Uses Java SAX/DOM parsers
- **Gantt**: Uses expat (C library)

### Task Dispatch

- **Ant**: Reflection-based task loading
- **Gantt**: Direct function dispatch for built-in tasks, `execve()` for external

### Java Compilation

Both tools ultimately invoke `javac`, so compilation time should be similar.
The difference is in how the command is constructed and invoked.

## Expected Results

Based on typical benchmarks:

| Scenario | Expected Speedup |
|----------|-----------------|
| Startup/no-op | 20-50x |
| Property expansion | 10-30x |
| File operations | 5-20x |
| Clean build (small) | 3-10x |
| Clean build (large) | 1.5-3x |
| Incremental (up-to-date) | 10-30x |

*Speedups decrease as actual work (Java compilation) dominates*

## Bottleneck Analysis

If you're optimizing gantt, focus on:

1. **XML parsing** - Is expat the bottleneck?
2. **Property expansion** - String operations, hash table lookups
3. **Fileset evaluation** - Directory traversal, pattern matching
4. **Task dispatch** - Fork/exec overhead for external executables
5. **Path resolution** - Converting paths, checking existence

## Requirements

- **gantt** - Built in parent directory (`make`)
- **Apache Ant** - In PATH
- **Java JDK** - For compilation benchmarks
- **Perl** - For high-resolution timing
- **gumdrop** - Test project at `~/cpkb/gumdrop`

## Adding New Benchmarks

To add a new benchmark to `run-benchmarks.sh`:

1. Create a function `benchmark_<name>()` 
2. Use `run_benchmark` for timing with multiple iterations
3. Add case to the argument handler
4. Log results to the CSV file

Example:

```bash
benchmark_mytest() {
    print_header "Benchmark: My Test"
    
    # Setup...
    
    print_section "Apache Ant"
    local ant_time
    ant_time=$(run_benchmark "ant-mytest" "$ITERATIONS" ant -f build.xml target)
    
    print_section "Gantt"  
    local gantt_time
    gantt_time=$(PATH="$GANTT_SCRIPTS:$PATH" run_benchmark "gantt-mytest" "$ITERATIONS" "$GANTT_BIN" -f build.xml target)
    
    # Report results...
}
```

