#!/bin/sh

echo "Building 32bit PIN on Ubuntu Trusty (14.04)"

docker build --no-cache -t patricknevindwyer/pin-ubuntu32-trusty:latest -f Dockerfile_32bit .