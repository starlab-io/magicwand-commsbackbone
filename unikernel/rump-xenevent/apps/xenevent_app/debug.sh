#!/bin/sh

##
## Refer to setup_net.sh 
##

echo "If debugging, use:"
echo "gdb -tui -ex 'target remote:1234' xenevent.run"

#sudo ../rumprun/bin/rumprun xen -di -I xen0,xenif -W xen0,inet,static,10.190.2.24/24 

#FOR MARK
#IP=192.168.0.18

#FOR ALEX
IP=10.0.2.138

# rumprun -S xen -dip -D 1234 -M 128 -N client-rump \
#         -I if,tap0,'-net tap,script=no,ifname=tap0' \
#         -W if,inet,static,10.0.120.101/24 \
#         client.run

rumprun -S xen -dip -D 1234 -M 512 -N xenevent-rump \
        -I xen0,xenif \
        -W xen0,inet,static,$IP/24 \
        xenevent.run

		
#rumprun -S xen -dip -D 1234 -M 512 -N xenevent-rump \
#        -I xen0,xenif \
#        -W xen0,inet,dhcp \
#        xenevent.run
