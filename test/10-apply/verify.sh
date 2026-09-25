#!/bin/sh
# Copyright (C) 2026 Chris Burdess <dog@gnu.org>
# SPDX-License-Identifier: GPL-3.0-or-later
# Verify apply test outputs

SRC_DIR="$(dirname "$0")/src"
BUILD_DIR="$(dirname "$0")/build"

# Check source files were created
for i in 1 2 3; do
    if [ ! -f "$SRC_DIR/file$i.txt" ]; then
        echo "ERROR: file$i.txt not created"
        exit 1
    fi
done

echo "All apply tests passed"
exit 0

