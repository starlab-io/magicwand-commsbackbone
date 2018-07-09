#!/bin/bash

IMAGE_NAME=apache2-performance-image-ins
CONTAINER_NAME=apache2-performance-container-ins

sudo docker rm -f $CONTAINER_NAME

sudo docker run						\
	-dit 						\
	--security-opt seccomp=unconfined               \
	--privileged					\
	--network=host					\
	--device=/dev/mwcomms				\
	--name $CONTAINER_NAME				\
	$IMAGE_NAME
