#!/bin/sh

##
## Refer to setup_net.sh 
##

#FOR ALEX
IP=10.0.2.138

echo "Running xenvent without debugging enabled"
echp "IP address: $IP"

rumprun -S xen -di -M 256 -N xenevent-rump \
        -I xen0,xenif \
        -W xen0,inet,static,$IP/24 \
        xenevent.run
