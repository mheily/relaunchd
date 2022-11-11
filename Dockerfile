#
# A container with the relaunchd source code
#
FROM alpine:3.14
RUN apk add --no-cache binutils gcc g++ libgcc gdb cmake git pkgconfig bash make mandoc

RUN mkdir /tmp/build
WORKDIR /tmp/build

COPY ./ /tmp/src/
