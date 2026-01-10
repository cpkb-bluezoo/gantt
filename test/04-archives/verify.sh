#!/bin/sh
# Verify archive test outputs

BUILD_DIR="$(dirname "$0")/build"

# Check archives were created
for f in test.zip test.tar test.tar.gz to-gzip.txt.gz to-bzip.txt.bz2; do
    if [ ! -f "$BUILD_DIR/$f" ]; then
        echo "ERROR: $f not created"
        exit 1
    fi
done

# Check extractions worked
for d in unzipped untarred untarred-gz gunzipped bunzipped; do
    if [ ! -d "$BUILD_DIR/$d" ]; then
        echo "ERROR: $d directory not created"
        exit 1
    fi
done

# Check extracted files exist - they may be in nested paths
unzipped_count=$(find "$BUILD_DIR/unzipped" -name "readme.txt" 2>/dev/null | wc -l)
if [ "$unzipped_count" -eq 0 ]; then
    echo "ERROR: no unzipped readme.txt found"
    exit 1
fi

untarred_count=$(find "$BUILD_DIR/untarred" -name "readme.txt" 2>/dev/null | wc -l)
if [ "$untarred_count" -eq 0 ]; then
    echo "ERROR: no untarred readme.txt found"
    exit 1
fi

echo "All archive tests passed"
exit 0


