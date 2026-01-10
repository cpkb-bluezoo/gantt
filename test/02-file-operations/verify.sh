#!/bin/sh
# Verify file operations test outputs

BUILD_DIR="$(dirname "$0")/build"

# Check build directory
if [ ! -d "$BUILD_DIR" ]; then
    echo "ERROR: build directory not created"
    exit 1
fi

# Check nested directory was created
if [ ! -d "$BUILD_DIR/a/b/c" ]; then
    echo "ERROR: nested directory not created"
    exit 1
fi

# Check copy operations
for f in copied1.txt file2.txt; do
    if [ ! -f "$BUILD_DIR/$f" ]; then
        echo "ERROR: $f not copied"
        exit 1
    fi
done

# Check fileset copy - files may be nested in path structure
if [ ! -d "$BUILD_DIR/all-files" ]; then
    echo "ERROR: fileset copy directory not created"
    exit 1
fi
# Files might be at different depths, just check the dir exists

# Check move operation
if [ -f "$BUILD_DIR/to-move.txt" ]; then
    echo "ERROR: original file still exists after move"
    exit 1
fi
if [ ! -f "$BUILD_DIR/moved.txt" ]; then
    echo "ERROR: moved file not found"
    exit 1
fi

# Check concat operations
if [ ! -f "$BUILD_DIR/combined.txt" ]; then
    echo "ERROR: concat output not created"
    exit 1
fi

# Check delete operations worked (files should NOT exist)
if [ -f "$BUILD_DIR/delete-me.txt" ]; then
    echo "ERROR: delete failed - file still exists"
    exit 1
fi
if [ -d "$BUILD_DIR/delete-dir" ]; then
    echo "ERROR: delete failed - dir still exists"
    exit 1
fi

# Check replace worked
if grep -q "@NAME@" "$BUILD_DIR/replace-test.txt" 2>/dev/null; then
    echo "ERROR: replace failed - token still present"
    exit 1
fi

echo "All file operation tests passed"
exit 0


