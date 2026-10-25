#!/bin/sh
# BananaTube: the host tests.
#   sh tests/run.sh
# Builds (in ./build) and runs every test. No DRM, no GPU, no network: the display half and the backends are
# built but only the device-independent code is exercised.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${BUILD:-$HERE/build}
cmake -S "$HERE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j"$(nproc)" >/dev/null
cd "$BUILD"
ctest --output-on-failure
