#!/bin/sh

##
## STAR LAB PROPRIETARY & CONFIDENTIAL
## Copyright (C) 2016, Star Lab — All Rights Reserved
## Unauthorized copying of this file, via any medium is strictly prohibited.
##

##
## Refer to setup_net.sh 
## Use debug.sh for a debug run
##

target=ins-rump.run

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

#rumprun -T /tmp/rump.tmp -S xen -di -M 256 -N mw-ins-rump \
#        -I xen0,xenif \
#        -W xen0,inet,static,$RUMP_IP/8,$_GW \
#        ins-rump.run
rumprun -S xen -di -M 2048 -N mw-ins-rump \
        -I xen0,xenif \
        -W xen0,inet,static,$RUMP_IP/24,$_GW \
        $target
