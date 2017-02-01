#!/bin/bash

##
## Source this file in the current shell. Do not check in changes.
##
## This sets env variables that are used by other scripts.
##

setmwaddr() {                                                              
    export PVM_IP=10.15.32.102
    export RUMP_IP=10.15.32.103
}

setmwaddr
