TAG = relaunchd-src:latest
BUILD_VOL = relaunchd-build
VOLUMES = -v $$(pwd)/..:/tmp/src:ro -v $(BUILD_VOL):/tmp/build:rw
BUILD_TYPE ?= Release
STRIP ?= yes

test: buildvol
	docker build -t $(TAG) -f ../Dockerfile ..
	docker run --rm $(VOLUMES) -it $(TAG) sh -ex -c '\
        cd /tmp/build ; \
        /tmp/src/configure --build-type=release --objdir=/tmp/build --prefix=/ --mandir=/usr/share/man; \
        ls -la ; ls -l src ; \
        cat -n Makefile ; \
        make -j all install ; \
        [ "$(STRIP)" != "yes" ] || strip /sbin/launchd ; \
        ldd launchd ; \
        ls -lh /sbin/launchd /bin/launchctl'

check: buildvol
	docker build -t $(TAG)-ubuntu -f Dockerfile.Ubuntu ..
	docker run --rm $(VOLUMES) -it $(TAG)-ubuntu /tmp/src/run-precommit-checks.sh /tmp/build

check-asan:
	docker run --rm $(VOLUMES) -it $(TAG)-ubuntu sh -ex -c '\
        /tmp/src/configure --objdir=/tmp/build --enable-asan; \
        make -C /tmp/build -j all check CXXFLAGS=-Werror '

check-msan:
	docker run --rm $(VOLUMES) -it $(TAG)-ubuntu sh -ex -c '\
        /tmp/src/configure --objdir=/tmp/build --enable-msan; \
        make -C /tmp/build -j all check CXXFLAGS=-Werror CXX=clang++-15'

clean:
	docker rmi -f $(TAG) || true
	docker volume rm -f $(BUILD_VOL)

shell:
	docker run --rm $(VOLUMES) -it $(TAG) bash

shell-ubuntu:
	docker run --rm $(VOLUMES) -it $(TAG)-ubuntu bash

# Recreate the build volume
buildvol:
	docker volume rm -f $(BUILD_VOL)
	docker volume create $(BUILD_VOL)

.PHONY : test clean shell buildvol
