#!/bin/bash

##
## First build rump (build-rr.sh xen), then source this script once:
## > source ./RUMP_ENV.sh
##
## You should be able to source it regardless of your current directory
##

script_dir=$(readlink -f "${BASH_SOURCE[0]}")
rump_dir=$(dirname "$script_dir")

# Are we in the right directory?
echo "Assuming that Rump is in $rump_dir"

test_file=$rump_dir/buildrump.sh
if ! [ -d $test_file ]; then
    echo "********************************"
    echo "FAILURE: couldn't find $test_file!"
    echo "********************************"
    return
fi

# Remove all PATH entries that contain 'rumprun'
newpath=

for d in `echo $PATH | sed -e "s/:/ /g"`; do
    if [ -d $bindir ]; then
        if ! echo $d | /bin/grep -q rump; then
            newpath=$d:$newpath
        fi
    fi
done


setgitbranch() {
    GIT=git

    # are we on a git branch which is not master?
    if type ${GIT} >/dev/null; then
        GITBRANCH=$(${GIT} rev-parse --abbrev-ref HEAD 2>/dev/null)
        if [ ${GITBRANCH} = "master" -o ${GITBRANCH} = "HEAD" ]; then
            GITBRANCH=
        else
            echo "Detected git branch \"$GITBRANCH\""
            GITBRANCH=-${GITBRANCH}
        fi
    else
        GITBRANCH=
    fi
    echo "Rerun this script if you switch git branches"
}
setgitbranch

# Now, update the PATH to include the needed subdirectories here
export PATH=$rump_dir/rumprun$GITBRANCH/bin:$rump_dir/obj-amd64-xen$GITBRANCH/rumptools/bin:$newpath
echo "New path: $PATH"

export RUMPROOT=$rump_dir
export RUMPRUN_WARNING_STFU=please

##
## The build system doesn't quite act the way I'd expect. These
## functions help use it the "correct" way, so far as I can tell.
##

echo "Standard build with: build-rr.sh xen"
dbgbuildrump() {
    echo "Building rump; build log is build.log"
    ./build-rr.sh xen -- -F DBG=-ggdb > build.log
}
export -f dbgbuildrump
echo "Command dbgbuildrump is available"

dbgrebuildrump() {
    setgitbranch
    rm -fr obj-amd64-xen$GITBRANCH   \
        platform/xen/obj             \
        platform/xen/xen/include/xen \
        rumprun$GITBRANCH

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


buildapitags() {
    # Build etags for available API
    rm -f TAGS
    find -L apps include platform/xen/xen src-netbsd/sys/sys src-netbsd/include \
        -name "*.[ch]" -type f -print \
        | etags --output=TAGS --declarations --language=c --members --append -
}
export -f buildapitags
echo "Command buildapitags is available"

buildtags() {
    # Build etags for available API
    rm -f TAGS
    find -L apps include platform src-netbsd lib \
        -name "*.[ch]" -type f -print \
        | etags --output=TAGS --declarations --language=c --members --append -
}
export -f buildtags
echo "Command buildtags is available"
