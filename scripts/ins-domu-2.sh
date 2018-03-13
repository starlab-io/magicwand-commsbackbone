#!/bin/bash
set -e

echo "----------------------------------------------------------------------"
echo "   Executing Part 2 of DomU INS script (Inserting Kernel Module) ..."
echo "----------------------------------------------------------------------"

#echo "using password ${DOMU_PASSWORD}"
echo ${DOMU_PASSWORD} | sudo -S echo sudo enabled

cd ~/${PRODUCTION_DIR}/magicwand-commsbackbone/protvm/kernel/mwcomms/
sudo insmod mwcomms.ko

echo ""
echo " dmesg:"
dmesg

echo "  End DomU script part 2"
