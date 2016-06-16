#!/bin/sh

echo "Starting 32bit mode with BASH"
mkdir output
chmod ugo+rwx output
docker stop apache-pin
docker rm apache-pin

OUTPUT=$(PWD)/output
echo "Writing trace files to $OUTPUT"

docker run --privileged -v $OUTPUT:/root/output -it --name apache-pin patricknevindwyer/pin-ubuntu32-trusty:latest bash