#!/bin/sh

##
## Refer to setup_net.sh 
##

echo "If debugging, use:"
echo "gdb -tui -ex 'target remote localhost:1234' ins-rump.run"

if [ -z $RUMP_IP ]; then
    echo "Failure: RUMP_IP must be defined in env"
    exit 1
fi

if [ -z $_GW ]; then
    echo "Failure: _GW must be defined in env"
    exit 1
fi

rumprun -S xen -dip -D 1234 -M 512 -N mw-ins-rump \
        -I xen0,xenif \
        -W xen0,inet,static,$RUMP_IP/8,$_GW \
        ins-rump.run
