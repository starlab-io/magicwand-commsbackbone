#!/bin/bash

##
## STAR LAB PROPRIETARY & CONFIDENTIAL
## Copyright (C) 2016, Star Lab — All Rights Reserved
## Unauthorized copying of this file, via any medium is strictly prohibited.
##

##
## Loads the mwcomms driver. Its device will be useable by all.
##

driver=mwcomms
modpath=/sys/module/$driver

sudo rmmod $driver > /dev/null 2>&1

#sudo cp 010_mwcomms.rules /etc/udev/rules.d
sudo sh -c 'echo "MODE = \"0666\"" > /etc/udev/rules.d/010_$driver.rules'

sudo insmod $driver.ko

for s in ".text" ".bss"
do
    echo -n "section $s.... "
    sudo cat $modpath/sections/$s # need root access for this...
done

sudo tail -f /var/log/kern.log
