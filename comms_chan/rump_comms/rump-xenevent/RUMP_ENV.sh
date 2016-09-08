#!/bin/sh

##
## First build rump (build-rr.sh xen), then source this script once:
## 
## > source ./UPDATE_PATH.sh
##

export RUMPRUN_WARNING_STFU=please

export PATH=$PWD/rumprun/bin:$PWD/obj-amd64-xen/rumptools/bin:$PATH
