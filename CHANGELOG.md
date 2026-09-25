# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.0.0] - 2026-09-25

This is the first release to use strict [Semantic Versioning](https://semver.org/)
(MAJOR.MINOR.PATCH). It follows 1.0 (2005), which predates that scheme.

### Added
- GNU build system: `./configure && make && make check && make install`,
  with `make dist` producing `gantt-2.0.0.tar.gz`
- Support for Ivy dependency management tasks, including `ivy:publish` and the
  cache path, cache fileset and report tasks
- New SAX-style direct parser for improved performance
- Support for `<parallel>` and `<sequential>` tasks
- Support for `<condition>` task with nested conditions
- Support for `<native2ascii>` task
- Support for `<apply>` task (execon)
- Support for `<pathconvert>` task
- Support for `<xmlproperty>` task
- Support for `<javaversion>` condition
- Support for nested `<manifest>` in jar task
- Support for `<selector>` elements in javac task
- Incremental compilation support in javac task
- OS/platform filtering for exec task
- Input redirection support for exec and java tasks
- Property capture (outputproperty, errorproperty, resultproperty)
- Native C implementations for file operations (copy, delete, move, touch, chmod, mkdir, concat)
- Profiling support via GANTT_PROFILE environment variable

### Changed
- Licence is now the GNU GPL version 3 or later (previously stated as
  version 2 or later); all source file headers updated to match
- `gantt`, its compiled helpers (`gantt_copy`, `gantt_delete`, ...) and the
  helper scripts (`gantt_zip`, `gantt_tar`, ...) are all installed into the
  same directory (`$bindir`) and found through `PATH`; no separate
  `lib/gantt` directory or extra `PATH` setup
- Code is compiled as strict C99 (`-std=c99`)
- The version is defined once, in `configure.ac`
- Switched from libxml2 to expat for XML parsing
- Reorganized source code into `src/` directory
- Improved error messages and logging
- Better handling of property expansion in paths
- Enhanced fileset resolution with selector support

### Removed
- The hand-written `Makefile` (replaced by autoconf/automake)
- Git hash and build date in `-version` output

### Fixed
- `<echo>` is now a built-in task and honours `file` and `append`; previously
  it fell back to the system `echo`, which ignored them, so nothing was written
- Nested `<manifest>` in `<jar>` was dropped by the direct parser, producing
  JARs without `Main-Class`
- Nested tasks in `<parallel>` and `<sequential>` were dropped by the direct
  parser and never ran; fixed a double free when they were released
- Added the missing `untar`, `gzip`, `gunzip`, `bzip2` and `bunzip2` tasks
  (helper scripts)
- Proper handling of package-info.java in incremental builds
- Correct classpath separator on different platforms
- Memory leaks in various subsystems

## [1.0] - 2005-02-18

### Added
- Initial release
- Basic Ant build file parsing
- Support for common tasks: javac, jar, javadoc, java
- Property management
- File operations via shell scripts
- Target dependency resolution

[Unreleased]: https://github.com/cpkb-bluezoo/gantt/compare/v2.0.0...HEAD
[2.0.0]: https://github.com/cpkb-bluezoo/gantt/compare/v1.0...v2.0.0
[1.0]: https://github.com/cpkb-bluezoo/gantt/releases/tag/v1.0

