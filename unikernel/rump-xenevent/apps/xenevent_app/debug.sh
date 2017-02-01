#!/bin/sh

##
## Refer to setup_net.sh 
##

echo "If debugging, use:"
echo "gdb -tui -ex 'target remote:1234' xenevent.run"

if [ -z $RUMP_IP ]; then
    echo "Failure: PVM_IP must be defined in env"
    exit 1
fi

rumprun -S xen -dip -D 1234 -M 512 -N xenevent-rump \
        -I xen0,xenif \
        -W xen0,inet,static,$RUMP_IP/24 \
        xenevent.run
