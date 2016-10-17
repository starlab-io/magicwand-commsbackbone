#!/bin/sh

##
## Refer to setup_net.sh 
##

echo "If debugging, use:"
echo "gdb -ex 'target remote:1234' xenevent.run"

#rumprun -S xen -dip -D 1234 -M 256 -N mt_connect \
#    -I xen0,xenif \
#    -W xen0,inet,static,10.190.2.111/24 \
#    mt_connect.run

rumprun -S xen -di -D 1234 -M 256 -N mt_connect \
    -I xen0,xenif \
    -W xen0,inet,static,10.190.2.111/24 \
    mt_connect.run
