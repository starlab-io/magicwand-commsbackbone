#!/bin/bash

STATUS=""
XENSTORE_KEY="vm_evt_chn_is_bound"

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root" 
   exit 1
fi

while [ "$STATUS" != "1" ]
do
    temp="$(xenstore-ls | grep $XENSTORE_KEY)"
    temp="${temp%\"}"
    temp="${temp#*$XENSTORE_KEY = \"}"
    STATUS=$temp
done

echo "INS READY"
