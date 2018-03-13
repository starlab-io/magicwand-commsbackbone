#!/bin/bash
set -e

echo "----------------------------------------------------------------------"
echo "   Executing Part 3 of DomU INS script (Launching Apache) ..."
echo "----------------------------------------------------------------------"
cd ~

#echo "using password ${DOMU_PASSWORD}"
echo ${DOMU_PASSWORD} | sudo -S echo sudo enabled

sudo mkdir -p /var/log/output

sudo apt-get install apache2 ssl-cert -y

echo sudo apache2ctl stop
sudo apache2ctl stop

echo "sudo sh -c 'echo export LD_PRELOAD=$(pwd)/${PRODUCTION_DIR}/magicwand-commsbackbone/protvm/user/wrapper/tcp_ip_wrapper.so >> /etc/apache2/envvars'"
sudo sh -c 'echo export LD_PRELOAD='$(pwd)'/'${PRODUCTION_DIR}'/magicwand-commsbackbone/protvm/user/wrapper/tcp_ip_wrapper.so >> /etc/apache2/envvars'

echo "sudo sh -c 'echo Listen 0.0.0.0:80 > /etc/apache2/ports.conf'"
# sudo sh -c 'echo Listen 0.0.0.0:80 > /etc/apache2/ports.conf'
sudo sh -c 'echo Listen 80 > /etc/apache2/ports.conf'

echo sudo apache2ctl start
sudo apache2ctl start

echo "  End DomU script part 3"