# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
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
- Build metadata (git hash, build date) in version output

### Changed
- Switched from libxml2 to expat for XML parsing
- Reorganized source code into `src/` directory
- Improved error messages and logging
- Better handling of property expansion in paths
- Enhanced fileset resolution with selector support

### Fixed
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

[Unreleased]: https://github.com/dogburds/gantt/compare/v1.0...HEAD
[1.0]: https://github.com/dogburds/gantt/releases/tag/v1.0

