#!/bin/sh -ex
cd "$(dirname "$0")"
builddir="$(pwd)/cmake-build-coverage"
rm -rf "$builddir"
mkdir "$builddir"
cmake -B "$builddir" -S . -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=yes
cmake --build "$builddir" -j
cmake --build "$builddir" --target test_all_coverage
open "$builddir/test_all_coverage/index.html"
