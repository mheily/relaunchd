TAG = relaunchd-src:latest

test:
	docker build -t $(TAG) -f ../Dockerfile ..
	./build-with-private-depends.sh
	# FIXME: currently broken: ./build-with-external-depends.sh

clean:
	docker rmi $(TAG) || true

.PHONY : test clean
