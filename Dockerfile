FROM debian:latest
MAINTAINER mark@heily.com

RUN apt-get update && apt-get install -y build-essential cmake
RUN apt-get install -y git
RUN mkdir /build
COPY ./ /build/
WORKDIR /build
RUN rm -rf cmake-build-debug ; mkdir cmake-build-debug && cd cmake-build-debug && cmake .. && make
