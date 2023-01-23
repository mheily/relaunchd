#!/bin/sh -ex

export CXX

cd "$(dirname "$0")"

if [ -n "$1" ] ; then
  builddir="$1"
  mkdir -p "$builddir"
else
  builddir="$(mktemp -d /tmp/relaunchd-build-precommit.XXXXXXXX)"
  trap 'rm -rf "$builddir"' EXIT
fi

./configure --objdir="$builddir"

make -C "$builddir" clean clang-analyzer-report

make -C "$builddir" clean all check

# Sanitizers are not working on MacOS yet
if [ "$(uname)" != "Darwin" ] ; then
  make -C "$builddir" check-valgrind

  ./configure --objdir="$builddir" --enable-asan
  make -C "$builddir" clean check ASAN_OPTIONS=verbosity=1:detect_stack_use_after_return=1:atexit=1

  make -C "$builddir" clean check-ubsan

# DISABLED - false positives suspected. Try using valgrind instead.
#  ./configure --objdir="$builddir" --enable-msan
#  make -C "$builddir" clean check
fi

./configure --objdir="$builddir" --build-type=release
make -C "$builddir" clean all check

# TODO: would like this, but too many errors right now
#clang-tidy -format-style=file -header-filter=. -p $builddir --checks=\* src/*.cc
#run-clang-tidy -header-filter=.\* -p $builddir \
# -checks=bugprone\*,cert-\*,cppcoreguidelines-\*
# src/*.cc

echo "+++ SUCCESS: All tests passed +++"
