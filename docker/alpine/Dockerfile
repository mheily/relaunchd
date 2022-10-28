FROM relaunchd-src:latest

RUN mkdir -p /lib/relaunchd/LaunchDaemons/
COPY LaunchDaemons/ /lib/relaunchd/LaunchDaemons/

CMD '/sbin/launchd'
