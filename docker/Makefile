TAG = relaunchd-src:latest
BUILD_VOL = relaunchd-build
VOLUMES = -v $$(pwd)/..:/tmp/src:ro -v $(BUILD_VOL):/tmp/build:rw
BUILD_TYPE ?= Release
STRIP ?= yes

test: buildvol
	docker build -t $(TAG) -f ../Dockerfile ..
	docker run --rm $(VOLUMES) -it $(TAG) sh -ex -c '\
        cd /tmp/build ; \
        cmake /tmp/src -DCMAKE_BUILD_TYPE=$(BUILD_TYPE); \
        make -j ; \
        [ "$(STRIP)" != "yes" ] || strip src/launchd src/launchctl ; \
        ls -lh src/launch*'

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
