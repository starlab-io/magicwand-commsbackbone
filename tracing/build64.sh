#!/bin/sh

echo "Building 64bit PIN on Ubuntu Trusty (14.04)"

docker build --no-cache -t patricknevindwyer/pin-ubuntu64-trusty:latest -f Dockerfile_64bit .