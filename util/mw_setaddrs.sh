#!/bin/bash

##
## Source this file in the current shell. Do not check in changes.
##
## This sets env variables that are used by other scripts.
##

_NET=10.15.32
setmwaddr() {                                                              
    export HOST_IP=$_NET.101
    export  PVM_IP=$_NET.102
    export RUMP_IP=$_NET.109
}

setmwaddr
