#!/bin/bash

##
## Loads the mwcomms driver. Its device will be useable by all.
##

driver="mwcomms"

sudo rmmod $driver > /dev/null 2>&1

#sudo cp 010_mwcomms.rules /etc/udev/rules.d
sudo sh -c 'echo "MODE = \"0666\"" > /etc/udev/rules.d/010_$driver.rules'

sudo insmod $driver.ko
