# Gantt

A fast, lightweight replacement for Apache Ant written in C.

Gantt implements core Ant tasks natively while allowing additional tasks to be added as external executables. It parses standard Ant `build.xml` files and executes targets with minimal overhead.

## Design Philosophy

### Unix Philosophy

Gantt follows the Unix philosophy of small, composable tools:

- **Do one thing well**: The core binary handles XML parsing, dependency resolution, property expansion, and task dispatch. Actual file operations are delegated to external executables.
- **Composability**: Tasks can be implemented as shell scripts, compiled binaries, or any executable program. This allows customization without modifying or recompiling the core.
- **Text streams**: Task executables receive parameters via environment variables and communicate results through exit codes and standard streams.

### Minimal Dependencies

Unlike many modern build tools, Gantt has minimal external dependencies:

- **Expat** for XML parsing (included with macOS and most Unix systems)
- **Standard C library** (C99)
- **POSIX APIs** for process management and file operations

No runtime environments, package managers, or heavyweight frameworks required.

### Extensibility Without Bloat

Rather than bundling every possible task into a monolithic binary, Gantt uses a tiered approach:

1. **Built-in tasks**: Performance-critical Java toolchain tasks (`javac`, `java`, `jar`, `javadoc`) and property management are compiled into the binary.
2. **External executables**: File operations (`mkdir`, `copy`, `delete`, etc.) are handled by `gantt_*` scripts or system commands.
3. **Fallback to system commands**: When no custom script is found, Gantt falls back to standard Unix commands (`mkdir`, `cp`, `rm`, etc.).

This design keeps the core small while remaining fully extensible.

## Features

- Parses standard Ant `build.xml` files
- Property expansion with `${property}` syntax
- Target dependencies and conditional execution (`if`/`unless`)
- Filesets with include/exclude patterns
- Path definitions and references
- Built-in Java toolchain tasks
- Extensible task system via PATH executables

## Performance

Gantt's C implementation provides significant performance advantages, especially when paired with a native compiler like [Genesis](https://github.com/cpkb-bluezoo/genesis).

*Benchmarks run on macOS with OpenJDK 21.0.9 (Corretto), testing Gumdrop (642 Java source files).*

### Clean Builds

| Build Tool | Time | vs Ant+javac |
|------------|------|--------------|
| Apache Ant + javac | ~3.8s | baseline |
| Gantt + javac | ~4.2s | 0.9x |
| **Gantt + Genesis** | **~1.9s** | **2.0x faster** |

Gantt + javac is slightly slower than Ant for clean builds because Ant uses an in-process compiler API while Gantt forks external processes. However, pairing Gantt with the Genesis compiler eliminates all JVM overhead, cutting build time in half.

### Incremental Builds (1 file changed)

| Build Tool | Time | vs Ant+javac |
|------------|------|--------------|
| Apache Ant + javac | ~690ms | baseline |
| Gantt + javac | ~420ms | **1.6x faster** |
| **Gantt + Genesis** | **~100ms** | **7x faster** |

Even when recompiling a single changed file, Ant + javac spends most of its time on JVM startup. Gantt + Genesis completes the entire cycle in ~100ms.

### No-op Builds (nothing changed)

| Build Tool | Time | vs Ant+javac |
|------------|------|--------------|
| Apache Ant + javac | ~620ms | baseline |
| Gantt + javac | ~60ms | **10x faster** |
| Gantt + Genesis | ~80ms | **8x faster** |

When no files have changed, Gantt's C-based timestamp checking completes almost instantly, avoiding JVM startup entirely.

## Requirements

### All Platforms

- C99-compatible compiler (GCC, Clang, etc.)
- Make
- Expat XML parser library

### macOS

Expat is included with the system. No additional packages required.

```bash
# Xcode Command Line Tools provides the compiler
xcode-select --install
```

### Linux (Debian/Ubuntu)

```bash
sudo apt-get install build-essential libexpat1-dev
```

### Linux (RHEL/CentOS/Fedora)

```bash
sudo dnf install gcc make expat-devel
# or on older systems:
sudo yum install gcc make expat-devel
```

### Linux (Arch)

```bash
sudo pacman -S base-devel expat
```

### FreeBSD

```bash
pkg install expat
```

### OpenBSD/NetBSD

Expat is in the base system. Just ensure you have a C compiler.

## Building

```bash
# Clone or extract the source
cd gantt

# Build
make

# Run tests (optional)
make test
```

The build produces a single `gantt` executable.

### Build Options

Override compiler or flags if needed:

```bash
# Use a specific compiler
make CC=clang

# Add debug symbols
make CFLAGS="-Wall -Wextra -g -O0 -std=c99"

# Custom include/library paths (rarely needed)
make INCLUDES="-I/custom/include" LDFLAGS="-L/custom/lib"
```

## Installation

### System-wide Installation

```bash
# Install to /usr/local (default)
sudo make install

# Or specify a different prefix
sudo make install PREFIX=/opt/gantt
```

This installs:
- `gantt` binary to `$PREFIX/bin/`
- Task scripts to `$PREFIX/lib/gantt/`

### User Installation

```bash
make install PREFIX=$HOME/.local

# Add to your shell profile:
export PATH="$HOME/.local/bin:$HOME/.local/lib/gantt:$PATH"
```

### PATH Setup for Task Scripts

For Gantt to find the bundled task scripts (`gantt_mkdir`, `gantt_copy`, etc.), add them to your PATH:

```bash
# If installed system-wide:
export PATH="/usr/local/lib/gantt:$PATH"

# If using from source directory:
export PATH="/path/to/gantt/bin:$PATH"
```

Without this, Gantt falls back to system commands which work for basic operations but lack some features.

## Usage

```bash
# Run default target
gantt

# Run specific target
gantt compile

# Run multiple targets
gantt clean compile test

# Use a different build file
gantt -buildfile other.xml

# Show version
gantt -version

# Verbose output
gantt -verbose

# Set properties from command line
gantt -Dproperty=value target
```

### Custom Java Compiler

To use an alternative Java compiler (e.g., [Genesis](https://github.com/cpkb-bluezoo/genesis)), set the `JAVAC` environment variable:

```bash
export JAVAC=/path/to/genesis
gantt compile
```

The lookup order for the Java compiler is:
1. `executable` attribute on the `<javac>` task
2. `build.compiler` project property
3. `JAVAC` environment variable
4. `javac` in PATH

## Project Structure

```
gantt/
├── gantt.c          # Main entry point
├── project.c        # Project parsing and management
├── target.c         # Target resolution and execution
├── task.c           # Task dispatch
├── exec.c           # Process execution utilities
├── property.c       # Property task implementation
├── java_task.c      # Java task implementation
├── javac.c          # Javac task implementation
├── javadoc.c        # Javadoc task implementation
├── jar.c            # Jar task implementation
├── fileset.c        # Fileset handling
├── selector.c       # File selectors
├── path.c           # Path definitions
├── match.c          # Pattern matching
├── util.c           # Utility functions (lists, hashtables, strings)
├── xml.c            # Expat-based XML parser
├── bin/             # Task shell scripts
│   ├── gantt_mkdir
│   ├── gantt_copy
│   ├── gantt_delete
│   └── ...
└── doc/             # Additional documentation
    ├── EXECUTABLES.md   # Task executable system
    └── TASK_STATUS.md   # Implementation status
```

## Documentation

- [Task Executables](doc/EXECUTABLES.md) - How Gantt discovers and invokes task executables
- [Task Status](doc/TASK_STATUS.md) - Implementation status of Ant tasks

## Example Build File

```xml
<?xml version="1.0" encoding="UTF-8"?>
<project name="myproject" default="build" basedir=".">
    
    <property name="src" location="src"/>
    <property name="build" location="build"/>
    <property name="dist" location="dist"/>
    
    <target name="init">
        <mkdir dir="${build}"/>
    </target>
    
    <target name="compile" depends="init">
        <javac srcdir="${src}" destdir="${build}"/>
    </target>
    
    <target name="jar" depends="compile">
        <mkdir dir="${dist}"/>
        <jar destfile="${dist}/myproject.jar" basedir="${build}"/>
    </target>
    
    <target name="run" depends="jar">
        <java jar="${dist}/myproject.jar" fork="true">
            <arg value="--verbose"/>
        </java>
    </target>
    
    <target name="clean">
        <delete dir="${build}"/>
        <delete dir="${dist}"/>
    </target>
    
    <target name="build" depends="jar"/>
    
</project>
```

## Writing Custom Task Executables

You can extend Gantt by creating executables named `gantt_<taskname>` and placing them in your PATH. These can be shell scripts or compiled programs in any language.

See [doc/EXECUTABLES.md](doc/EXECUTABLES.md) for details.

## License

GNU General Public License v2.0 or later.

## Author

Chris Burdess <dog@gnu.org>

Originally written in February 2005. Refactored in 2026 to remove GLib and libxml2 dependencies.
