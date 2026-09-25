dnl configure.ac - autoconf input for gantt
dnl Copyright (C) 2005, 2026 Chris Burdess <dog@gnu.org>
dnl
dnl This file is part of gantt; see COPYING for the licence terms.

AC_PREREQ([2.69])
AC_INIT([gantt], [2.0], [dog@gnu.org], [gantt],
        [https://github.com/cpkb-bluezoo/gantt])
AC_CONFIG_SRCDIR([src/gantt.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])

AM_INIT_AUTOMAKE([gnu subdir-objects -Wall -Wno-portability])
AM_SILENT_RULES([yes])

AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
dnl gantt needs a C99 compiler.  Autoconf 2.70+ enables C99 or later as part
dnl of AC_PROG_CC; older versions need the explicit macro.  Either way, check
dnl by compiling some C99 rather than trusting a cache variable.
m4_version_prereq([2.70], [], [AC_PROG_CC_C99])
AC_CACHE_CHECK([whether the C compiler supports C99], [gantt_cv_c99],
  [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <stdbool.h>
#include <stdint.h>
struct pt { int x, y; };
static inline long long twice(long long v) { return v * 2; }
]], [[
  // C99 comment, mixed declarations, designated initialiser, for-scope decl
  struct pt p = { .y = 2 };
  bool ok = twice(p.y) == 4;
  for (int i = 0; i < 1; i++) { ok = ok && i == 0; }
  return ok ? 0 : 1;
]])], [gantt_cv_c99=yes], [gantt_cv_c99=no])])
AS_IF([test "x$gantt_cv_c99" != xyes],
  [AC_MSG_ERROR([a C99 compiler is required])])
AC_PROG_INSTALL

dnl Expat is the only library dependency.  Use CPPFLAGS/LDFLAGS to point at
dnl a non-standard installation.
AC_CHECK_HEADER([expat.h], [],
  [AC_MSG_ERROR([expat.h not found; install the Expat development files or set CPPFLAGS])])
AC_CHECK_LIB([expat], [XML_ParserCreate], [],
  [AC_MSG_ERROR([libexpat not found; install Expat or set LDFLAGS])])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
