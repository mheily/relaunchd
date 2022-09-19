FROM debian:sid
MAINTAINER mark@heily.com

RUN apt-get update && apt-get install -y build-essential cmake git pkg-config libkqueue-dev libucl-dev nlohmann-json3-dev vim less
USER nobody
RUN mkdir /tmp/build

