#!/bin/sh
# Verify ivy:publish test outputs

TARGET_DIR="$(dirname "$0")/publish-target"
JAR_DEST="$TARGET_DIR/test/myapp/2.0/myapp-2.0.jar"
IVY_DEST="$TARGET_DIR/test/myapp/2.0/ivy-2.0.xml"

# Artifact published to the pubrevision-stamped path, with its content
# unchanged (found via the descriptor's OWN declared revision, 1.0-SNAPSHOT,
# not pubrevision - a deliver/publish workflow relabels without renaming
# local build output first).
if [ ! -f "$JAR_DEST" ]; then
    echo "ERROR: artifact not published to pubrevision-based path"
    exit 1
fi
if ! grep -q "myapp 1.0-SNAPSHOT build output" "$JAR_DEST"; then
    echo "ERROR: published artifact content mismatch"
    exit 1
fi

# Descriptor published to the pubrevision-stamped path, but - the
# documented v1 fidelity gap - its CONTENT still shows the original
# authored revision, since gantt has no ivy.xml writer to rewrite it.
if [ ! -f "$IVY_DEST" ]; then
    echo "ERROR: descriptor not published to pubrevision-based path"
    exit 1
fi
if ! grep -q 'revision="1.0-SNAPSHOT"' "$IVY_DEST"; then
    echo "ERROR: published descriptor should still show its ORIGINAL authored revision"
    exit 1
fi
if grep -q 'revision="2.0"' "$IVY_DEST"; then
    echo "ERROR: published descriptor unexpectedly contains pubrevision - was it rewritten?"
    exit 1
fi

# Second run (the "overwrite-check" target, which re-runs the same
# publish): default overwrite=false must skip both files with a warning,
# not fail the build.
OUTPUT=$(gantt overwrite-check 2>&1)
if ! echo "$OUTPUT" | grep -q 'already exists.*overwrite="false"'; then
    echo "ERROR: expected an overwrite=false skip warning on rerun"
    exit 1
fi
if ! echo "$OUTPUT" | grep -q "published 0 file(s), 2 skipped, 0 failed"; then
    echo "ERROR: rerun should publish 0 files and skip both (artifact + descriptor)"
    exit 1
fi

echo "All ivy:publish tests passed"
exit 0
