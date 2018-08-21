#!/bin/bash

IMAGE_NAME=apache-performance-image-ins
CONTAINER_NAME=apache-performance-container-ins

sudo docker rm -f $CONTAINER_NAME

sudo docker run						\
	-dit 						\
	--security-opt seccomp=unconfined               \
	--privileged					\
	--mount type=bind,source="$(pwd)"/conf/htdocs,target=/opt/httpd/htdocs \
	--device=/dev/mwcomms				\
	--network=host					\
	--name $CONTAINER_NAME				\
	$IMAGE_NAME
