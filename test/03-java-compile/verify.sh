#!/bin/sh
# Verify Java compile test outputs

TEST_DIR="$(dirname "$0")"
BUILD_DIR="$TEST_DIR/build"
DIST_DIR="$TEST_DIR/dist"

# Check build directory
if [ ! -d "$BUILD_DIR" ]; then
    echo "ERROR: build directory not created"
    exit 1
fi

# Check class files were compiled
if [ ! -f "$BUILD_DIR/com/example/Hello.class" ]; then
    echo "ERROR: Hello.class not compiled"
    exit 1
fi

if [ ! -f "$BUILD_DIR/com/example/Util.class" ]; then
    echo "ERROR: Util.class not compiled"
    exit 1
fi

# Check JAR was created
if [ ! -f "$DIST_DIR/hello.jar" ]; then
    echo "ERROR: hello.jar not created"
    exit 1
fi

# Verify JAR contains expected files
if ! jar tf "$DIST_DIR/hello.jar" | grep -q "com/example/Hello.class"; then
    echo "ERROR: JAR doesn't contain Hello.class"
    exit 1
fi

# Verify manifest
if ! unzip -p "$DIST_DIR/hello.jar" META-INF/MANIFEST.MF | grep -q "Main-Class"; then
    echo "ERROR: JAR manifest missing Main-Class"
    exit 1
fi

echo "All Java compile tests passed"
exit 0


