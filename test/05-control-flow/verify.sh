#!/bin/sh
# Copyright (C) 2026 Chris Burdess <dog@gnu.org>
# SPDX-License-Identifier: GPL-3.0-or-later
# Verify control flow test outputs

BUILD_DIR="$(dirname "$0")/build"

# Check antcall worked
if [ ! -f "$BUILD_DIR/helper-called.txt" ]; then
    echo "ERROR: antcall did not work"
    exit 1
fi

# Check parallel tasks completed
for i in 1 2 3; do
    if [ ! -f "$BUILD_DIR/parallel$i.txt" ]; then
        echo "ERROR: parallel task $i did not complete"
        exit 1
    fi
done

# Check sequential tasks completed
for i in 1 2 3; do
    if [ ! -f "$BUILD_DIR/seq$i.txt" ]; then
        echo "ERROR: sequential task $i did not complete"
        exit 1
    fi
done

echo "All control flow tests passed"
exit 0


