#!/bin/sh
# Copyright (C) 2026 Chris Burdess <dog@gnu.org>
# SPDX-License-Identifier: GPL-3.0-or-later
# Verify property test outputs

BUILD_DIR="$(dirname "$0")/build"

# Check build directory was created
if [ ! -d "$BUILD_DIR" ]; then
    echo "ERROR: build directory not created"
    exit 1
fi

# Check files were created
for f in message.txt test.properties config.xml length-test.txt source.txt target.txt; do
    if [ ! -f "$BUILD_DIR/$f" ]; then
        echo "ERROR: $f not created"
        exit 1
    fi
done

echo "All expected files present"
exit 0


