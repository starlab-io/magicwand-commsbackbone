#!/bin/sh

echo "Starting Apache with PIN - 32bit"
mkdir output
chmod ugo+rwx output
docker stop apache-pin
docker rm apache-pin

OUTPUT=$(PWD)/output
echo "Writing trace files to $OUTPUT"

docker run --privileged -v $OUTPUT:/root/output -dt --name apache-pin patricknevindwyer/pin-ubuntu32-trusty:latest