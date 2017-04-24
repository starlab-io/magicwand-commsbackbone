#!/bin/bash

##
## Builds the XD3 INS: the Rump unikernel itself and the INS
## application. The final output of this script is the Rump INS
## unikernel located at:
##
## $insapp_dir/ins-rump.run
##

source _setenv

if [ -z $MWROOT ]; then
   echo "Failure: MWROOT must be defined in env"
   exit 1
fi

insapp_dir=$MWROOT/ins/ins-rump/apps/ins-app
insapp_makefile=$insapp_dir/Makefile

docmd() {
   echo "PWD: $PWD"
   echo "Command: $*"
   $*
}

##
## Build Rump
##
cd $MWROOT/ins/ins-rump

source RUMP_ENV.sh > /dev/null

docmd dbgbuildrump


##
## Build INS app
##

# Derive the makefile if it's not there
if [ ! -f $insapp_dir/Makefile ]; then
    $MWROOT/util/mw_prep
fi

cd $insapp_dir

docmd make
