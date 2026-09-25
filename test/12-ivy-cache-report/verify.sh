#!/bin/sh
# Copyright (C) 2026 Chris Burdess <dog@gnu.org>
# SPDX-License-Identifier: GPL-3.0-or-later
# Verify ivy:cachepath / ivy:cachefileset / ivy:report test outputs
#
# ivy:cachepath/ivy:cachefileset results are checked via a fresh gantt run's
# console output (from the "cachepath result: .../cachefileset result: ..."
# <echo message> lines), not a written file - gantt's <echo file="..."> does
# not actually write to disk (it falls back to the system echo command,
# which ignores the file= attribute), a pre-existing, unrelated gap.

BUILD_DIR="$(dirname "$0")/build"
REPORT_FILE="$BUILD_DIR/reports/test-cacherpt-default.xml"

OUTPUT=$(gantt 2>&1)

if ! echo "$OUTPUT" | grep -q "cachepath result:.*libA-2.0.jar"; then
    echo "ERROR: ivy:cachepath's registered path is missing the conflict winner libA-2.0.jar"
    exit 1
fi
if echo "$OUTPUT" | grep "cachepath result:" | grep -q "libA-1.0.jar"; then
    echo "ERROR: ivy:cachepath's registered path should not contain the conflict loser libA-1.0.jar"
    exit 1
fi

if ! echo "$OUTPUT" | grep -q "cachefileset result:.*libA-2.0.jar"; then
    echo "ERROR: ivy:cachefileset's registered fileset is missing the conflict winner libA-2.0.jar"
    exit 1
fi
if echo "$OUTPUT" | grep "cachefileset result:" | grep -q "libA-1.0.jar"; then
    echo "ERROR: ivy:cachefileset's registered fileset should not contain the conflict loser libA-1.0.jar"
    exit 1
fi

# ivy:report: the emitted XML must show the conflict resolution outcome,
# the unresolvable dependency's error, and license/homepage/pubdate info
# from both a POM (libA, <licenses>+<url>) and a real ivy.xml descriptor
# (libE, <info homepage= pubdate=><license/></info>).
if [ ! -f "$REPORT_FILE" ]; then
    echo "ERROR: report file not created"
    exit 1
fi
if ! grep -q '<evicted-by rev="2.0"/>' "$REPORT_FILE"; then
    echo "ERROR: report missing evicted-by for the conflict loser"
    exit 1
fi
if ! grep -q 'error="not found"' "$REPORT_FILE"; then
    echo "ERROR: report missing the error entry for the unresolvable dependency"
    exit 1
fi
if ! grep -q '<license name="Apache-2.0"' "$REPORT_FILE"; then
    echo "ERROR: report missing the POM-derived license (libA)"
    exit 1
fi
if ! grep -q '<license name="MIT"' "$REPORT_FILE"; then
    echo "ERROR: report missing the ivy.xml-derived license (libE)"
    exit 1
fi
if ! grep -q 'homepage="https://example.org/libE"' "$REPORT_FILE"; then
    echo "ERROR: report missing libE's homepage"
    exit 1
fi
if ! grep -q 'pubdate="20260101120000"' "$REPORT_FILE"; then
    echo "ERROR: report missing libE's pubdate"
    exit 1
fi

echo "All ivy cache/report tests passed"
exit 0
