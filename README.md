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

Gantt's C implementation can provide significant performance advantages, particularly for incremental builds.

### Incremental Builds

When source files haven't changed, Gantt is **6-8x faster** than Apache Ant:

| Project | Gantt | Ant | Speedup |
|---------|-------|-----|---------|
| Gumdrop (592 files) | ~60ms | ~500ms | 8x |
| Tomcat (1749 files) | ~70ms | ~410ms | 6x |

This speed comes from Gantt's C-based timestamp checking that avoids JVM startup overhead entirely.

### Clean Builds

For clean builds, performance is comparable since both tools invoke the same `javac` compiler:

| Project | Gantt | Ant |
|---------|-------|-----|
| Gumdrop (592 files) | ~2.0s | ~2.1s |
| Tomcat (1749 files) | ~4.3s | ~4.3s |

**Note**: Projects with multiple `javac` invocations may see a small performance penalty (~0.5-1s) because Gantt spawns separate JVM processes for each compilation, while Ant uses an in-process compiler API. This trade-off enables Gantt's dramatically faster incremental builds.

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

Chris Burdess <dog@bluezoo.org>

Originally written in February 2005. Refactored in 2026 to remove GLib and libxml2 dependencies.
