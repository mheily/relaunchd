#!/bin/sh

tag="${1:-relaunchd-src:latest}"

docker run -it $tag bash -ex -c '
    cd /tmp/build
    rm -rf cmake-build-debug
    mkdir cmake-build-debug
    cd cmake-build-debug
    cmake -DUSE_PRIVATE_DEPENDENCIES=OFF ..
    make
'
