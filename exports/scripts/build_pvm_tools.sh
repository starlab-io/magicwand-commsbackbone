#!/bin/bash








#Script to build magicwand shim and driver
source _setenv

if [ -z $MWROOT ]; then
    echo "Failure: MWROOT must be defined in env"
    exit 1
fi

shim_dir=$MWROOT/protvm/user/wrapper
shim_makefile=$shim_dir/Makefile

driver_dir=$MWROOT/protvm/kernel/mwcomms
driver_makefile=$driver_dir/Makefile

docmd()
{
    echo "PWD: $PWD"
    echo "Command: $*"
    $*
}


if [ ! -f $shim_makefile ]; then
    $MWROOT/util/mw_prep
fi

cd $shim_dir

docmd make


if [ ! -d /lib/modules/$(uname -r)/build ]; then
    echo "/lib/modules/$(uname -r)/build not found , please install linux headers"
fi

cd $driver_dir

docmd make
