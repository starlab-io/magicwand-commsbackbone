#!/bin/sh

##
## First build rump (build-rr.sh xen), then source this script once:
## > source ./RUMP_ENV.sh
##
## It MUST be run from Rump's root directory (where this script is
## located).
##

newpath=

# Remove all PATH entries that contain 'rumprun'
for d in `echo $PATH | sed -e "s/:/ /g"`; do
    if [ -d $bindir ]; then
        if ! echo $d | /bin/grep -q rump; then
            #echo "$d does not contain rump"
            newpath=$d:$newpath
        #else
        #    echo "$d does contain rump"
        fi
    fi
done

PATH=$newpath

export PATH=$PWD/rumprun/bin:$PWD/obj-amd64-xen/rumptools/bin:$PATH
echo "New path: $PATH"

export RUMPROOT=$PWD
export RUMPRUN_WARNING_STFU=please
