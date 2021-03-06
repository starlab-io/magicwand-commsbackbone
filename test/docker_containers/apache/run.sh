#!/bin/bash

IMAGE_NAME=apache-performance-image
CONTAINER_NAME=apache-performance-container

sudo docker rm -f $CONTAINER_NAME

sudo docker run							\
	-dit 							\
	--security-opt seccomp=unconfined               	\
	--privileged						\
	--mount type=bind,source="$(pwd)"/conf/htdocs,target=/opt/httpd/htdocs \
	--network=host						\
	--name $CONTAINER_NAME					\
	$IMAGE_NAME
