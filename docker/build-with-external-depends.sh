#!/bin/sh

cd "$(dirname "$0")"

tag="${1:-relaunchd-src:latest}"

docker run -v $(pwd)/..:/tmp/src:ro -it $tag bash -ex -c '
    cd /tmp/build
    cmake -DUSE_PRIVATE_DEPENDENCIES=OFF /tmp/src
    make
'
