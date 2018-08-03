#!/bin/bash


source ./_setenv

#Clear out the entire magicwand root path
sudo xenstore-rm $KEYSTORE_ROOT

#Create the xenstore root with no key value
sudo xenstore-write $KEYSTORE_ROOT ""

#Give DomU's read and write permissions
sudo xenstore-chmod $KEYSTORE_ROOT b
