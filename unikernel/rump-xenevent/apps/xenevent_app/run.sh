#!/bin/sh

##
## Refer to setup_net.sh 
##

if -z $RUMP_IP; then
    echo "Failure: PVM_IP must be defined in env"
    exit 1
fi

echo "Running xenvent without debugging enabled"
echo "IP address: $RUMP_IP"

rumprun -S xen -di -M 256 -N xenevent-rump \
        -I xen0,xenif \
        -W xen0,inet,static,$RUMP_IP/24 \
        xenevent.run
