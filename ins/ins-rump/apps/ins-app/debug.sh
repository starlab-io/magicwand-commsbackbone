#!/bin/sh

##
## Refer to setup_net.sh 
##

target=ins-rump.run

echo "If debugging, use:"
echo "gdb -tui -ex 'target remote localhost:1234' $target"

if [ -z $RUMP_IP ]; then
    echo "Failure: RUMP_IP must be defined in env"
    exit 1
fi

if [ -z $_GW ]; then
    echo "Failure: _GW must be defined in env"
    exit 1
fi

#$cpu_arg="vcpu-set 1,2"
#$cpu_arg="vcpus=2"

rumprun -S xen -dip -D 1234 -M 512 -N mw-ins-rump \
        -I xen0,xenif \
        -W xen0,inet,static,$RUMP_IP_2/8,$_GW \
        $target
