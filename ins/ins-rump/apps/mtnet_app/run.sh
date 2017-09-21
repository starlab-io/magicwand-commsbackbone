#!/bin/sh

##
## Refer to setup_net.sh 
##

echo "If debugging, use:"
echo "gdb -ex 'target remote:1234' mt_connect.run"

IP=10.30.30.65

# no debug
rumprun -S xen -i -M 256 -N mt_connect \
    -I xen0,xenif \
    -W xen0,inet,static,$IP/24 \
    mt_connect.run

#DEBUG
#rumprun -S xen -di -D 1234 -M 256 -N mt_connect \
#    -I xen0,xenif \
#    -W xen0,inet,static,$IP/24 \
#    mt_connect.run
