TAG = relaunchd-src:latest
BUILD_VOL = relaunchd-build
VOLUMES = -v $$(pwd)/..:/tmp/src:ro -v $(BUILD_VOL):/tmp/build:rw
BUILD_TYPE ?= Release
STRIP ?= yes

test: buildvol
	docker build -t $(TAG) -f ../Dockerfile ..
	docker run --rm $(VOLUMES) -it $(TAG) sh -ex -c '\
        cd /tmp/build ; \
        /tmp/src/configure --build-type=release --objdir=/tmp/build --prefix=/; \
        ls -la ; ls -l src ; \
        cat -n Makefile ; \
        make -j all install ; \
        [ "$(STRIP)" != "yes" ] || strip /sbin/launchd ; \
        ldd launchd ; \
        ls -lh /sbin/launchd /bin/launchctl'

clean:
	docker rmi -f $(TAG) || true
	docker volume rm -f $(BUILD_VOL)

shell:
	docker run --rm $(VOLUMES) -it relaunchd-src:latest bash

# Recreate the build volume
buildvol:
	docker volume rm -f $(BUILD_VOL)
	docker volume create $(BUILD_VOL)

.PHONY : test clean shell buildvol
