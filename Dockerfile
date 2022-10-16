FROM alpine:3.14
RUN apk add --no-cache binutils gcc g++ libgcc cmake git pkgconfig bash make

RUN mkdir /tmp/build
WORKDIR /tmp/build

COPY ./ /tmp/src/

RUN cmake -DCMAKE_INSTALL_PREFIX=/ /tmp/src && make && make install
