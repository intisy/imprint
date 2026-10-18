#!/bin/sh
set -eu
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Debug -DIMPRINT_SANITIZE=ON -DIMPRINT_WARNINGS_AS_ERRORS=ON >/dev/null
cmake --build build/linux --parallel
if [ $# -gt 0 ]; then
    build/linux/tests/imprint-tests "--filter=$1"
else
    ctest --test-dir build/linux --output-on-failure
fi
