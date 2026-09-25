#!/bin/sh
# Copyright (C) 2026 Chris Burdess <dog@gnu.org>
# SPDX-License-Identifier: GPL-3.0-or-later
# Generate configure and Makefile.in from configure.ac / Makefile.am.
# Only needed when building from a git checkout; release tarballs already
# contain the generated files.
set -e
cd "$(dirname "$0")"
mkdir -p m4
exec autoreconf --install --verbose
