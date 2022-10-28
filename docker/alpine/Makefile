TAG = relaunchd-alpine:latest

all:
	docker build -t $(TAG) -f Dockerfile .

clean:
	docker rmi -f $(TAG) || true
	if [ -e container-stamp ] ; then \
  		docker rm -fv "$$(cat container-stamp)" ; rm -f container-stamp ; fi

container-stamp:
	docker run -d $(TAG) > container-stamp

shell: container-stamp
	docker exec -it "$$(cat container-stamp)" bash

.PHONY : all clean shell
