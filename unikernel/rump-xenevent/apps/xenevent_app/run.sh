#!/bin/sh

##
## Refer to setup_net.sh 
##

IP=192.168.0.17
IP=10.15.32.8

echo "Running xenvent without debugging enabled"
echo "IP address: $IP"

rumprun -S xen -di -M 256 -N xenevent-rump \
        -I xen0,xenif \
        -W xen0,inet,static,$IP/24 \
        xenevent.run
