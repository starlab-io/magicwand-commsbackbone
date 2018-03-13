#!/bin/sh

##
## Refer to setup_net.sh 
##

target=ins-rump.run
port=2221

echo "If debugging, use:"
echo "gdb -tui -ex 'target remote localhost:$port' $target"

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

rumprun -S xen -dip -D $port -M 3084 -N mw-ins-rump \
        -I xen0,xenif \
        -W xen0,inet,static,$RUMP_IP/8,$_GW \
        $target
