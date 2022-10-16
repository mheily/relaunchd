#!/bin/bash

set -euo pipefail

cd "$(dirname "$0")"

tag="${1:-relaunchd-src:latest}"

docker run -v $(pwd)/..:/tmp/src:ro -it $tag sh -ex -c '
    rm -rf /tmp/build || true
    mkdir /tmp/build
    cd /tmp/build
    cmake /tmp/src
    make
    make install
'
