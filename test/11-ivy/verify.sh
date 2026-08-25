#!/bin/sh
# Verify ivy:resolve / ivy:retrieve test outputs

BUILD_DIR="$(dirname "$0")/build"

# (c) Conflict resolution: libA 2.0 (pulled in via libD) must win over
# libA 1.0 (pulled in via the direct dependency, libB, and libC) - only
# the winner's artifact should ever be fetched into the cache or retrieved.
if [ ! -f "$BUILD_DIR/cache/com.example/libA/jars/libA-2.0.jar" ]; then
    echo "ERROR: conflict winner libA-2.0.jar was not cached"
    exit 1
fi
if [ -f "$BUILD_DIR/cache/com.example/libA/jars/libA-1.0.jar" ]; then
    echo "ERROR: conflict loser libA-1.0.jar should never have been fetched"
    exit 1
fi
if [ ! -f "$BUILD_DIR/lib/default/libA-2.0.jar" ]; then
    echo "ERROR: conflict winner libA-2.0.jar was not retrieved"
    exit 1
fi
if [ -f "$BUILD_DIR/lib/default/libA-1.0.jar" ]; then
    echo "ERROR: conflict loser libA-1.0.jar should never have been retrieved"
    exit 1
fi

# (a)/(b) Plain and transitive m2compatible POM dependencies
if [ ! -f "$BUILD_DIR/lib/default/libB-1.0.jar" ]; then
    echo "ERROR: libB-1.0.jar (direct) was not retrieved"
    exit 1
fi
if [ ! -f "$BUILD_DIR/lib/default/libD-2.0.jar" ]; then
    echo "ERROR: libD-2.0.jar (direct) was not retrieved"
    exit 1
fi

# (d) Real ivy.xml module descriptor via a non-m2compatible resolver -
# proves this isn't just POM parsing.
if [ ! -f "$BUILD_DIR/cache/org.example/nativemod/ivys/ivy-1.0.xml" ]; then
    echo "ERROR: native ivy.xml descriptor was not cached"
    exit 1
fi
if [ ! -f "$BUILD_DIR/lib/default/nativemod-1.0.jar" ]; then
    echo "ERROR: native-descriptor module's jar was not retrieved"
    exit 1
fi

# (e) Alternate namespace prefix (<x:retrieve>, a different pattern/output
# dir) resolved to the same task as <ivy:resolve>/<ivy:retrieve>.
if [ ! -f "$BUILD_DIR/lib-alt/default/libA-2.0.jar" ]; then
    echo "ERROR: alternate-prefix retrieve (<x:retrieve>) did not run"
    exit 1
fi

echo "All ivy tests passed"
exit 0
