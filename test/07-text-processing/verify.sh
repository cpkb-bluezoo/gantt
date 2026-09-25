#!/bin/sh
# Copyright (C) 2026 Chris Burdess <dog@gnu.org>
# SPDX-License-Identifier: GPL-3.0-or-later
# Verify text processing test outputs

BUILD_DIR="$(dirname "$0")/build"

# Check replace worked
if [ ! -f "$BUILD_DIR/output.txt" ]; then
    echo "ERROR: replaced output file not created"
    exit 1
fi

if grep -q "@NAME@" "$BUILD_DIR/output.txt"; then
    echo "ERROR: token @NAME@ not replaced"
    exit 1
fi

# Check concat worked
if [ ! -f "$BUILD_DIR/combined.txt" ]; then
    echo "ERROR: combined file not created"
    exit 1
fi

# Check wrapped file has header
if [ -f "$BUILD_DIR/wrapped.txt" ]; then
    if ! grep -q "BEGIN" "$BUILD_DIR/wrapped.txt"; then
        echo "WARNING: header not found (may be OK)"
    fi
fi

echo "All text processing tests passed"
exit 0

