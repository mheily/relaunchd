TAG = relaunchd-src:latest
BUILD_VOL = relaunchd-build
VOLUMES = -v $$(pwd)/..:/tmp/src:ro -v $(BUILD_VOL):/tmp/build:rw
BUILD_TYPE ?= Release
STRIP ?= yes

test:
	test -n "$$(docker volume list -q -f name=relaunchd-build)" || docker volume create $(BUILD_VOL)
	docker build -t $(TAG) -f ../Dockerfile ..
	docker run $(VOLUMES) -it $(TAG) sh -ex -c '\
        cd /tmp/build ; \
        cmake /tmp/src -DCMAKE_BUILD_TYPE=$(BUILD_TYPE); \
        make -j ; \
        [ "$(STRIP)" != "yes" ] || strip src/launchd src/launchctl ; \
        ls -lh src/launch* ; \
        make install'


clean:
	docker rmi -f $(TAG) || true
	docker volume rm $(BUILD_VOL)

shell:
	docker run $(VOLUMES) -it relaunchd-src:latest bash

# Recreate the build volume
buildvol:
	if ! docker volume inspect $(BUILDVOL) >/dev/null 2>&1 ; then docker volume rm $(BUILD_VOL) ; fi
	docker volume create $(BUILD_VOL)

.PHONY : test clean shell buildvol
