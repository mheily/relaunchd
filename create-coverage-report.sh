#!/bin/sh -ex
#
# Copyright (c) 2022 Mark Heily <mark@heily.com>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

cd "$(dirname "$0")"
builddir="/tmp/relaunchd-build-coverage"
mkdir -p "$builddir"
test -O "$builddir"
rm -rf "$builddir"
mkdir "$builddir"
builddir=$(realpath "$builddir")
./configure --enable-coverage --objdir="$builddir"
make -j -C "$builddir" coverage-report

if [ "$(uname)" = "Darwin" ] ; then
  open "$builddir/coverage/index.html"
else
  echo "To view the coverage report, open $builddir/coverage/index.html in a web browser."
fi
