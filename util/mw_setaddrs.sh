#!/bin/bash

##
## Source this file in the current shell. Do not check in changes.
##
## This sets env variables that are used by other scripts.
##

setmwaddr() {                                                              
    export PVM_IP=192.168.0.14
    export RUMP_IP=192.168.0.15
}

setmwaddr
