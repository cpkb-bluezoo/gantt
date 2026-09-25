# Makefile.am - automake input for gantt
# Copyright (C) 2005, 2026 Chris Burdess <dog@gnu.org>
#
# This file is part of gantt; see COPYING for the licence terms.

ACLOCAL_AMFLAGS = -I m4

# Strict C99; this comes after any -std= that configure put into CC, so it wins.
AM_CFLAGS = -std=c99 -Wall -Wextra
AM_CPPFLAGS = -I$(srcdir)/src

# gantt finds its helpers (gantt_copy, gantt_zip, ...) through $PATH, so they
# are installed alongside it in $(bindir).
bin_PROGRAMS = gantt

# Native file operations: one source compiled once per task.
bin_PROGRAMS += \
	gantt_copy \
	gantt_delete \
	gantt_move \
	gantt_touch \
	gantt_chmod \
	gantt_mkdir \
	gantt_concat

# Task scripts that are not compiled.
dist_bin_SCRIPTS = \
	bin/gantt_filterchain \
	bin/gantt_replace \
	bin/gantt_replaceregexp \
	bin/gantt_setproxy \
	bin/gantt_sleep \
	bin/gantt_tar \
	bin/gantt_unzip \
	bin/gantt_war \
	bin/gantt_zip

gantt_SOURCES = \
	src/util.c \
	src/xml.c \
	src/project.c \
	src/target.c \
	src/task.c \
	src/exec.c \
	src/fileset.c \
	src/selector.c \
	src/path.c \
	src/property.c \
	src/match.c \
	src/javac.c \
	src/javadoc.c \
	src/jar.c \
	src/java_task.c \
	src/tstamp.c \
	src/fail.c \
	src/native2ascii.c \
	src/ivy_parse.c \
	src/ivy_resolve.c \
	src/ivy_cache_tasks.c \
	src/ivy_report.c \
	src/ivy_publish.c \
	src/parser.c \
	src/gantt.c \
	src/gantt.h \
	src/ivy.h \
	src/util.h \
	src/xml.h

gantt_copy_SOURCES = src/fileops.c
gantt_copy_CPPFLAGS = $(AM_CPPFLAGS) -DTASK_COPY
gantt_delete_SOURCES = src/fileops.c
gantt_delete_CPPFLAGS = $(AM_CPPFLAGS) -DTASK_DELETE
gantt_move_SOURCES = src/fileops.c
gantt_move_CPPFLAGS = $(AM_CPPFLAGS) -DTASK_MOVE
gantt_touch_SOURCES = src/fileops.c
gantt_touch_CPPFLAGS = $(AM_CPPFLAGS) -DTASK_TOUCH
gantt_chmod_SOURCES = src/fileops.c
gantt_chmod_CPPFLAGS = $(AM_CPPFLAGS) -DTASK_CHMOD
gantt_mkdir_SOURCES = src/fileops.c
gantt_mkdir_CPPFLAGS = $(AM_CPPFLAGS) -DTASK_MKDIR
gantt_concat_SOURCES = src/fileops.c
gantt_concat_CPPFLAGS = $(AM_CPPFLAGS) -DTASK_CONCAT

# Regression tests.  run-tests.sh copies the projects into the build tree, so
# the source tree is never written to (needed for VPATH builds and distcheck).
check-local: all
	GANTT_BUILDDIR='$(abs_builddir)' $(SHELL) $(srcdir)/test/run-tests.sh

EXTRA_DIST = \
	autogen.sh \
	CHANGELOG.md \
	CONTRIBUTING \
	README.md \
	SECURITY.md \
	benchmarks \
	test

clean-local:
	-rm -rf test-work

# Ship a ChangeLog generated from the git history, and keep generated test
# output and editor/OS litter out of the tarball.
dist-hook:
	if test -e $(srcdir)/.git; then \
	  rm -f $(distdir)/ChangeLog; \
	  (cd $(srcdir) && git log --date=short \
	    --format='%ad  %an  <%ae>%n%n%w(78,8,8)%s%n') \
	    > $(distdir)/ChangeLog; \
	fi
	find $(distdir)/test $(distdir)/benchmarks -type d \
	  \( -name build -o -name dist -o -name publish-target \
	     -o -name '$${basedir}' \) \
	  -prune -exec rm -rf {} +
	find $(distdir)/test $(distdir)/benchmarks \
	  \( -name .DS_Store -o -name '*~' \) -type f -exec rm -f {} +
