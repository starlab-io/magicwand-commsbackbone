#!/bin/bash

IMAGE_NAME=apache2-performance-image
CONTAINER_NAME=apache2-performance-container

sudo docker rm -f $CONTAINER_NAME

sudo docker run						\
	-dit 						\
	--security-opt seccomp=unconfined               \
	--privileged					\
	--network=host					\
	--name $CONTAINER_NAME				\
	$IMAGE_NAME
