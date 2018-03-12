#!/bin/bash

if [[ -z $PVM_CONF ]]; then
    PVM_CONF=/etc/xen/pvm.cfg
fi

if [[ -z $PVM_HOSTNAME ]]; then
    PVM_HOSTNAME=pvm
fi

if [[ -z $startdir ]]; then
    startdir=$(pwd)
fi

echo
echo Pinging PVM to see if it exists
echo

ping -c 3 $PVM_IP

rc=$?

if [[ rc -ne 0 ]] ; then
    echo PVM does not exist...
else
    echo PVM exists already. Killing it.
    sudo xl destroy $PVM_HOSTNAME
fi

echo Creating PVM...

sudo xl create $PVM_CONF
sudo xl destroy mw-ins-rump

cd $MWROOT
echo
echo cd $(pwd)
echo

#git pull

cd $MWROOT/util
echo
echo cd $(pwd)
echo

./mw_setkeys
./mw_prep

for i in {1,2,3,4,5}; do
    ./mw_copy_to_pvm
    if [[ $? -eq 0 ]]; then
        break
    elif [[ $i -eq 5 ]]; then
        echo Couldn\'t copy to PVM.
        exit 1
    fi
done

echo

# Build on PVM
ssh $PVM_USER@$PVM_IP "cd ~/protvm/kernel/mwcomms && make clean all"
ssh $PVM_USER@$PVM_IP "cd ~/protvm/user/tcp_ip_server && make clean all"
ssh $PVM_USER@$PVM_IP "cd ~/protvm/user/http_server && make clean all"

cd $MWROOT/test/apache_bench
echo
echo cd $(pwd)
echo

./make_files.sh

cd $MWROOT/ins/ins-rump
echo
echo cd $(pwd)
echo

command -v dbgrebuildrump

if [[ $? -ne 0 ]] ; then
    source RUMP_ENV.sh
    echo
fi

#dbgrebuildrump

cd apps/ins-app
echo
echo cd $(pwd)
echo

make clean all
