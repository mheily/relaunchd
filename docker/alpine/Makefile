TAG = relaunchd-alpine:latest

all:
	docker build -t $(TAG) -f Dockerfile .

clean: stop
	docker rmi -f $(TAG) || true

container-stamp:
	docker run --cap-add=SYS_PTRACE -d $(TAG) > container-stamp

restart: stop
	$(MAKE) container-stamp

stop:
	if [ -e container-stamp ] ; then \
  		docker rm -fv "$$(cat container-stamp)" ; rm -f container-stamp ; fi

shell: container-stamp
	docker exec -it "$$(cat container-stamp)" bash

logs: container-stamp
	docker logs "$$(cat container-stamp )"

.PHONY : all clean logs shell stop restart
