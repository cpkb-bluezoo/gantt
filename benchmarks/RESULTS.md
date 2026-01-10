# Benchmark Results: Gantt vs Apache Ant

## Summary

| Benchmark | Ant | Gantt | Speedup |
|-----------|-----|-------|---------|
| Startup (no-op) | 292 ms | 17 ms | **16.9x faster** |
| Property expansion (150 props) | 302 ms | 30 ms | **10.1x faster** |
| Clean build (gumdrop, 591 files) | 2,538 ms | 4,000 ms | 0.63x (slower) |
| Incremental build (no changes) | 543 ms | 1,300 ms | 0.42x (slower) |

## Analysis

### Where Gantt Excels

1. **Startup Time (17x faster)**
   - Native C binary vs JVM startup
   - ~275ms saved on every invocation
   - Massive win for simple tasks or scripts that invoke the build tool frequently

2. **XML Parsing & Property Expansion (10x faster)**
   - Expat (C library) vs Java SAX/DOM
   - Efficient hash table implementation
   - No reflection overhead

3. **Up-to-date Detection (more accurate)**
   - Gantt correctly detects all 591 files as up-to-date
   - Ant still recompiles 46 files (package-info.java handling)

### Where Gantt is Slower

1. **Clean Build (1.6x slower)**
   - Root cause: Process spawning overhead
   - Each `javac`, `jar`, `copy`, etc. spawns external processes
   - Ant runs everything in-process using the same JVM

2. **Incremental Build (2.4x slower)**
   - Checking 591 files for up-to-date status takes time
   - Directory scanning overhead for package-info.java detection
   - Still much faster than before (was 6x slower before implementing up-to-date checks)

## Incremental Compilation (Implemented)

Gantt now supports proper incremental compilation:

- Compares .java source file timestamps vs .class file timestamps
- Only recompiles files where source is newer than class
- Special handling for `package-info.java` files (which may not generate .class files)
- Correctly handles source files that define types with different names

Example output when all files are up-to-date:
```
    [javac] All 591 source files are up-to-date
```

Example output when one file was touched:
```
    [javac] Compiling 1 source file to /path/to/build (590 up-to-date)
```

## Remaining Bottlenecks

### 1. Up-to-date Check Overhead

Scanning 591 files for timestamp comparison takes ~1 second. Potential optimizations:
- Cache file timestamps in a `.gantt-cache` file
- Use filesystem change notifications (inotify/kqueue)
- Batch stat() calls

### 2. External Process Overhead

Gantt spawns external processes for:
- `javac` - spawns JVM process
- `jar` - spawns jar command
- File operations via `gantt_*` scripts

Each process spawn has ~10-20ms overhead. For gumdrop's build:
- ~600 javac invocations (one per file in Ant, one total in gantt)
- But gantt pays JVM startup for javac once, same as Ant

### 3. JAR Task Uses External Command

Ant's jar task uses Java's built-in ZIP APIs.
Gantt's jar task spawns the `jar` command.

The difference isn't huge for single JAR operations, but adds up.

## Recommendations

### High Priority

1. **~~Implement incremental compilation in javac task~~** ✅ DONE
   - Compare .java mtime vs .class mtime
   - Only pass modified sources to javac
   - Improvement achieved: 3x for incremental builds

### Medium Priority

2. **Cache file timestamps**
   - Store timestamps in `.gantt-cache` file
   - Only stat files that might have changed
   - Would reduce incremental build overhead

3. **Consider built-in JAR/ZIP support**
   - Use libzip or similar C library
   - Avoid process spawn overhead for archive operations

4. **Parallel compilation**
   - Split source files across multiple javac processes
   - Can utilize multiple CPU cores

### Low Priority

5. **File operation batching**
   - Batch multiple copy/delete operations
   - Reduce script invocation overhead

## When to Use Gantt

Gantt is ideal for:
- Quick tasks (startup overhead dominates)
- Scripts that invoke the build tool frequently
- Systems where JVM startup is expensive
- Embedded/resource-constrained environments

Apache Ant is better for:
- Large compilation tasks
- Incremental development workflows
- When in-process execution matters

## Bugs Fixed During Benchmarking

Several bugs were discovered and fixed while setting up benchmarks:

1. **Path resolution in resolve_path()** - Variables weren't being expanded
2. **Selector pattern expansion** - Include/exclude patterns weren't expanded
3. **basedir handling** - project->base_dir wasn't being set
4. **Fileset path resolution** - Relative paths weren't resolved against basedir
5. **JAR task basedir** - Wasn't using expand_location()
6. **JAR fileset stripping** - Needed to use expanded paths consistently
7. **delete task** - Failed when directory didn't exist (Ant succeeds silently)

## Features Implemented During Benchmarking

1. **Incremental compilation in javac task** (`javac.c`)
   - Compares source .java timestamp vs .class timestamp
   - Only compiles files where source is newer
   - Special handling for `package-info.java` files
   - Handles source files that define multiple/different type names
   - Reports how many files are up-to-date vs need compilation

