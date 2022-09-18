#FROM debian:latest # no kqueue
FROM debian:sid
MAINTAINER mark@heily.com

RUN apt-get update && apt-get install -y build-essential cmake git pkg-config libkqueue-dev nlohmann-json3-dev
RUN mkdir /build
COPY ./ /build/
WORKDIR /build

# Test 1: Build with private deps
#RUN rm -rf cmake-build-debug ; mkdir cmake-build-debug && cd cmake-build-debug && cmake .. && make

# Test 2: Build with external deps
#RUN rm -rf cmake-build-debug ; mkdir cmake-build-debug && cd cmake-build-debug && cmake -DUSE_PRIVATE_DEPENDENCIES=OFF .. && make
