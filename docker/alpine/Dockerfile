FROM relaunchd-src:latest

RUN mkdir -p /lib/relaunchd/LaunchDaemons/
COPY LaunchDaemons/ /lib/relaunchd/LaunchDaemons/

WORKDIR /tmp/build

RUN cmake -DCMAKE_INSTALL_PREFIX=/ /tmp/src && make install

ENTRYPOINT /sbin/launchd -v
