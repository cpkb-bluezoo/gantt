#!/bin/sh
# Verify web application test outputs

DIST_DIR="$(dirname "$0")/dist"
BUILD_DIR="$(dirname "$0")/build"

# Check WAR was created
if [ ! -f "$DIST_DIR/test.war" ]; then
    echo "ERROR: WAR file not created"
    exit 1
fi

# Check WAR contents were extracted
if [ ! -f "$BUILD_DIR/war-contents/WEB-INF/web.xml" ]; then
    echo "ERROR: web.xml not found in WAR"
    exit 1
fi

if [ ! -f "$BUILD_DIR/war-contents/index.html" ]; then
    echo "ERROR: index.html not found in WAR"
    exit 1
fi

echo "All web application tests passed"
exit 0

