#!/bin/bash

##
## First build rump (build-rr.sh xen), then source this script once:
## > source ./RUMP_ENV.sh
##
## It MUST be run from Rump's root directory (where this script is
## located).
##


# Are we in the right directory?

RUMP_DIR=$PWD

TEST_DIR=$RUMP_DIR/buildrump.sh
if ! [ -d $TEST_DIR ]; then
    echo "********************************"
    echo "FAILURE: couldn't find $TEST_DIR!"
    echo "********************************"
fi

# Remove all PATH entries that contain 'rumprun'
newpath=

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

# Now, update the PATH to include the needed subdirectories here
export PATH=$PWD/rumprun-performance_tests/bin:$PWD/obj-amd64-xen-performance_tests/rumptools/bin:$newpath
echo "New path: $PATH"

export RUMPROOT=$PWD
export RUMPRUN_WARNING_STFU=please

##
## The build system doesn't quite act the way I'd expect. These
## functions help use it the "correct" way, so far as I can tell.
##

dbgbuildrump() {
    ./build-rr.sh xen -- -F DBG=-ggdb > build.log
}
export -f dbgbuildrump
echo "Command dbgbuildrump is available"

dbgrebuildrump() {
    rm -fr obj-amd64-xen             \
        platform/xen/obj             \
        platform/xen/xen/include/xen \
        rumprun

    dbgbuildrump
}
export -f dbgrebuildrump
echo "Command dbgrebuildrump is available"

debugrump() {
    if [ -z $1 ]; then
        echo "WARNING: expected debug target binary as argument but not given."
    fi
    gdb -ex 'target remote:1234' $*
}
export -f debugrump
echo "Command debugrump <app> is available"


buildtags() {
    # Build etags for available API
    rm -f TAGS
    find -L apps include platform/xen/xen src-netbsd/sys/sys src-netbsd/include \
        -name "*.[ch]" -type f -print \
        | etags --output=TAGS --declarations --language=c --members --append -
}
export -f buildtags
echo "Command buildtags is available"
