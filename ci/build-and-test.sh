#!/bin/sh
set -eu

sh ci/test-linux.sh

cmake -S . -B build/windows -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DIMPRINT_STATIC=ON -DIMPRINT_WARNINGS_AS_ERRORS=ON
cmake --build build/windows --parallel
ctest --test-dir build/windows --output-on-failure

cmake -S . -B build/musl -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/musl.cmake \
      -DIMPRINT_STATIC=ON -DIMPRINT_WARNINGS_AS_ERRORS=ON
cmake --build build/musl --parallel
ctest --test-dir build/musl --output-on-failure
if readelf --dynamic build/musl/src/engine/imprint-engine | grep -q NEEDED; then
    echo "imprint-engine links shared libraries, it must be static" >&2
    exit 1
fi
