# Makefile for gantt
# Uses expat for XML parsing (included with macOS and most Unix systems)
#
# Build options:
#   make              - Debug build (with -g)
#   make release      - Optimized build (with -O2, no debug symbols)
#   make DEBUG=0      - Same as release
#   make dist         - Create release tarball

CC ?= cc
DEBUG ?= 1

# Version information (must match src/gantt.h)
VERSION_MAJOR = 2
VERSION_MINOR = 0
VERSION = $(VERSION_MAJOR).$(VERSION_MINOR)

# Git information for development builds
GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
GIT_DIRTY := $(shell git diff --quiet 2>/dev/null || echo "-dirty")
BUILD_DATE := $(shell date -u +%Y-%m-%d)

# Build flags
ifeq ($(DEBUG),1)
CFLAGS = -Wall -Wextra -g -std=c99
else
CFLAGS = -Wall -Wextra -O2 -std=c99
endif

# Add build metadata to development builds
VERSION_FLAGS = -DGANTT_GIT_HASH=\"$(GIT_HASH)$(GIT_DIRTY)\" -DGANTT_BUILD_DATE=\"$(BUILD_DATE)\"

# On macOS, expat is part of the system - no special paths needed
# On Linux, it's typically in /usr/include and /usr/lib
# Override these if needed:
#   make INCLUDES="-I/path/to/include" LDFLAGS="-L/path/to/lib"
INCLUDES ?=
LDFLAGS ?=
LIBS = -lexpat

# Directory structure
SRCDIR = src
OBJDIR = obj
BINDIR = bin
DISTDIR = dist

# Project name for distribution
PROJECT = gantt
DISTNAME = $(PROJECT)-$(VERSION)

# Source files
SOURCES = \
    util.c \
    xml.c \
    project.c \
    target.c \
    task.c \
    exec.c \
    fileset.c \
    selector.c \
    path.c \
    property.c \
    match.c \
    javac.c \
    javadoc.c \
    jar.c \
    java_task.c \
    tstamp.c \
    fail.c \
    native2ascii.c \
    ivy_parse.c \
    parser.c \
    gantt.c

OBJECTS = $(addprefix $(OBJDIR)/,$(SOURCES:.c=.o))

# Main target
all: $(OBJDIR) gantt fileops

$(OBJDIR):
	mkdir -p $(OBJDIR)

gantt: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)

# Native file operations - compiled C binaries (NOT shell scripts)
# These replace shell scripts for efficiency. Shell scripts like gantt_replace,
# gantt_tar, gantt_zip etc. should remain in bin/ and are NOT managed by make.
FILEOPS_BINARIES = $(BINDIR)/gantt_copy $(BINDIR)/gantt_delete $(BINDIR)/gantt_move $(BINDIR)/gantt_touch $(BINDIR)/gantt_chmod $(BINDIR)/gantt_mkdir $(BINDIR)/gantt_concat

fileops: $(FILEOPS_BINARIES)
	@$(RM) -r $(BINDIR)/*.dSYM 2>/dev/null || true

$(BINDIR)/gantt_copy: $(SRCDIR)/fileops.c
	$(CC) $(CFLAGS) -DTASK_COPY -o $@ $<

$(BINDIR)/gantt_delete: $(SRCDIR)/fileops.c
	$(CC) $(CFLAGS) -DTASK_DELETE -o $@ $<

$(BINDIR)/gantt_move: $(SRCDIR)/fileops.c
	$(CC) $(CFLAGS) -DTASK_MOVE -o $@ $<

$(BINDIR)/gantt_touch: $(SRCDIR)/fileops.c
	$(CC) $(CFLAGS) -DTASK_TOUCH -o $@ $<

$(BINDIR)/gantt_chmod: $(SRCDIR)/fileops.c
	$(CC) $(CFLAGS) -DTASK_CHMOD -o $@ $<

$(BINDIR)/gantt_mkdir: $(SRCDIR)/fileops.c
	$(CC) $(CFLAGS) -DTASK_MKDIR -o $@ $<

$(BINDIR)/gantt_concat: $(SRCDIR)/fileops.c
	$(CC) $(CFLAGS) -DTASK_CONCAT -o $@ $<

# Pattern rule for object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/gantt.h $(SRCDIR)/util.h $(SRCDIR)/xml.h
	$(CC) $(CFLAGS) $(VERSION_FLAGS) $(INCLUDES) -I$(SRCDIR) -c $< -o $@

# Dependencies (header files)
$(OBJDIR)/util.o: $(SRCDIR)/util.c $(SRCDIR)/util.h
$(OBJDIR)/xml.o: $(SRCDIR)/xml.c $(SRCDIR)/xml.h $(SRCDIR)/util.h
$(OBJDIR)/gantt.o: $(SRCDIR)/gantt.c $(SRCDIR)/gantt.h $(SRCDIR)/util.h $(SRCDIR)/xml.h
$(OBJDIR)/project.o: $(SRCDIR)/project.c $(SRCDIR)/gantt.h
$(OBJDIR)/target.o: $(SRCDIR)/target.c $(SRCDIR)/gantt.h
$(OBJDIR)/task.o: $(SRCDIR)/task.c $(SRCDIR)/gantt.h
$(OBJDIR)/exec.o: $(SRCDIR)/exec.c $(SRCDIR)/gantt.h
$(OBJDIR)/fileset.o: $(SRCDIR)/fileset.c $(SRCDIR)/gantt.h
$(OBJDIR)/selector.o: $(SRCDIR)/selector.c $(SRCDIR)/gantt.h
$(OBJDIR)/path.o: $(SRCDIR)/path.c $(SRCDIR)/gantt.h
$(OBJDIR)/property.o: $(SRCDIR)/property.c $(SRCDIR)/gantt.h
$(OBJDIR)/match.o: $(SRCDIR)/match.c $(SRCDIR)/gantt.h
$(OBJDIR)/javac.o: $(SRCDIR)/javac.c $(SRCDIR)/gantt.h
$(OBJDIR)/javadoc.o: $(SRCDIR)/javadoc.c $(SRCDIR)/gantt.h
$(OBJDIR)/jar.o: $(SRCDIR)/jar.c $(SRCDIR)/gantt.h
$(OBJDIR)/java_task.o: $(SRCDIR)/java_task.c $(SRCDIR)/gantt.h
$(OBJDIR)/tstamp.o: $(SRCDIR)/tstamp.c $(SRCDIR)/gantt.h
$(OBJDIR)/fail.o: $(SRCDIR)/fail.c $(SRCDIR)/gantt.h
$(OBJDIR)/native2ascii.o: $(SRCDIR)/native2ascii.c $(SRCDIR)/gantt.h
$(OBJDIR)/parser.o: $(SRCDIR)/parser.c $(SRCDIR)/gantt.h

# Clean up
clean:
	$(RM) gantt
	$(RM) -r $(OBJDIR)
	$(RM) $(FILEOPS_BINARIES)
	$(RM) -r $(BINDIR)/*.dSYM

# Deep clean (including distribution files)
distclean: clean
	$(RM) -r $(DISTDIR)

# Install (adjust PREFIX as needed)
PREFIX ?= /usr/local
install: gantt
	install -d $(PREFIX)/bin
	install -m 755 gantt $(PREFIX)/bin/gantt
	install -d $(PREFIX)/lib/gantt
	install -m 755 $(BINDIR)/gantt_* $(PREFIX)/lib/gantt/

# Uninstall
uninstall:
	$(RM) $(PREFIX)/bin/gantt
	$(RM) -r $(PREFIX)/lib/gantt

# Test build
test: gantt
	./gantt -version
	@echo "Running test suite..."
	cd test && ./run-tests.sh

# Release build (optimized, no debug symbols)
release:
	$(MAKE) DEBUG=0 clean all

# Create distribution tarball
dist: distclean
	mkdir -p $(DISTDIR)/$(DISTNAME)
	cp -r $(SRCDIR) $(DISTDIR)/$(DISTNAME)/
	cp -r $(BINDIR) $(DISTDIR)/$(DISTNAME)/
	cp Makefile README.md COPYING CONTRIBUTING CHANGELOG.md $(DISTDIR)/$(DISTNAME)/ 2>/dev/null || true
	mkdir -p $(DISTDIR)/$(DISTNAME)/test
	cp -r test/*.sh test/build.xml $(DISTDIR)/$(DISTNAME)/test/ 2>/dev/null || true
	cd $(DISTDIR) && tar -czf $(DISTNAME).tar.gz $(DISTNAME)
	$(RM) -r $(DISTDIR)/$(DISTNAME)
	@echo "Created $(DISTDIR)/$(DISTNAME).tar.gz"

# Print version information
version:
	@echo "gantt version $(VERSION)"
	@echo "Git: $(GIT_HASH)$(GIT_DIRTY)"

# Help target
help:
	@echo "gantt build system"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build gantt and helper binaries (default)"
	@echo "  release   - Build optimized release binaries"
	@echo "  install   - Install to PREFIX (default: /usr/local)"
	@echo "  uninstall - Remove installed files"
	@echo "  test      - Run test suite"
	@echo "  dist      - Create distribution tarball"
	@echo "  clean     - Remove build artifacts"
	@echo "  distclean - Remove build artifacts and distribution files"
	@echo "  version   - Print version information"
	@echo "  help      - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  DEBUG=0|1 - Debug build (default: 1)"
	@echo "  PREFIX    - Installation prefix (default: /usr/local)"
	@echo "  CC        - C compiler (default: cc)"

.PHONY: all clean distclean install uninstall test fileops release dist version help
