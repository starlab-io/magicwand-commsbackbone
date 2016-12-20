#!/bin/sh

##
## Refer to setup_net.sh 
##

echo "Running xenvent without debugging enabled"

#sudo ../rumprun/bin/rumprun xen -di -I xen0,xenif -W xen0,inet,static,10.190.2.24/24 
IP=10.1.2.18

# rumprun -S xen -dip -D 1234 -M 128 -N client-rump \
#         -I if,tap0,'-net tap,script=no,ifname=tap0' \
#         -W if,inet,static,10.0.120.101/24 \
#         client.run

rumprun -S xen -di -M 256 -N xenevent-rump \
        -I xen0,xenif \
        -W xen0,inet,dhcp \
        xenevent.run
