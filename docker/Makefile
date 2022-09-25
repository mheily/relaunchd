TAG = relaunchd-src:latest

test:
	docker build -t $(TAG) -f ../Dockerfile ..
	./build-with-private-depends.sh

	# FIXME: currently broken because libkqueue on Debian is too old
	#./build-with-external-depends.sh

clean:
	docker rmi -f $(TAG) || true

shell:
	docker run -v $$(pwd)/..:/tmp/src:ro -it relaunchd-src:latest bash

.PHONY : test clean shell
