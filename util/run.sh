#!/bin/sh

##
## Refer to setup_net.sh 
## Use debug.sh for a debug run
##

if [ -z $RUMP_IP ]; then

    if [ -z $1 ]; then
        echo "No RUMP_IP value found"
        exit 1
    fi

    RUMP_IP=$1
fi

if [ -z $_GW ]; then
    
    if [ -z $2 ]; then
        echo "No default gatway value"
    fi

    _GW=$2
fi

if [ -z $INS_DIR ]; then
    
    if [ -z $3 ]; then
        echo "No ins directory provided"
    fi

    INS_DIR=$3
fi

echo "Running xenvent without debugging enabled"
echo "IP address: $RUMP_IP"

rumprun -S xen -d -M 256 -N mw-ins-rump \
        -I xen0,xenif \
        -W xen0,inet,static,$RUMP_IP/8,$_GW \
        $INS_DIR/ins-rump.run
