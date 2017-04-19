#!/bin/sh

##
## Refer to setup_net.sh 
##

if [ -z $RUMP_IP ]; then
    echo "Failure: RUMP_IP must be defined in env"
    exit 1
fi

if [ -z $_GW ]; then
    echo "Failure: _GW must be defined in env"
    exit 1
fi

echo "Running xenvent without debugging enabled"
echo "IP address: $RUMP_IP"

rumprun -S xen -di -M 256 -N mw-ins-rump \
        -I xen0,xenif \
        -W xen0,inet,static,$RUMP_IP/24,$_GW \
        ins-rump.run
