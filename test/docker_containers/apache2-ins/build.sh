#!/bin/bash

echo "MWROOT=$MWROOT"

#INS
sudo rm -rf ./shim
mkdir -p ./shim
mkdir -p ./shim/protvm/user
cp -r $MWROOT/exports ./shim
cp -r $MWROOT/common ./shim
cp -r $MWROOT/protvm/user/wrapper ./shim/protvm/user
cp -r $MWROOT/protvm/common ./shim/protvm/

sudo docker build -t apache2-performance-image-ins .
