#!/bin/bash
set -e

echo "----------------------------------------------------------------------"
echo "   Executing Part 1 of DomU INS script..."
echo "----------------------------------------------------------------------"

#this will be the dir that contains the various git repos:
USER_NAME=$(whoami)

echo "----------------------------------------------------------------------"
echo "   DomU >> EXAMINING ENVIRONMENT, CREATING HOME DIRS"
echo "----------------------------------------------------------------------"

if ! [ -z "$MWROOT" ]; then
	if [ "$MWROOT" != "~/${PRODUCTION_DIR}/magicwand-commsbackbone" ] ; then
		echo "MWROOT is currently set to:"
		echo "  $MWROOT"
		echo "Temporarily changing it to:"
		echo "  ~/${PRODUCTION_DIR}/magicwand-commsbackbone"
		export MWROOT=~/${PRODUCTION_DIR}/magicwand-commsbackbone #this line a prereq for ./mw_prep
	fi
else
	export MWROOT=~/${PRODUCTION_DIR}/magicwand-commsbackbone #this line a prereq for ./mw_prep

fi

echo using pass ${DOMU_PASSWORD}
echo ${DOMU_PASSWORD} | sudo -S echo sudo enabled

# Create home dirs. chown/chgrp if dirs already exist:
if [ ! -d "/home/${USER_NAME}" ]; then
	echo "Creating home directories..."
	sudo -S mkdir ~
	sudo chown -R ${USER_NAME} ~
	sudo chgrp -R ${USER_NAME} ~
	cd ~
fi

# Set the GITHUB_TOKEN in bashrc:
if [ -z "$GITHUB_TOKEN" ]; then
    echo "Saving github token to DomU .bashrc for later"
    sed -i "/^export GITHUB_TOKEN=.*/d" ~/.bashrc
	sudo echo "export GITHUB_TOKEN=${GITHUB_TOKEN}" >> ~/.bashrc
fi  

echo ""
echo "Environment:"
env

echo "----------------------------------------------------------------------"
echo "   DomU >> INSTALLING PACKAGE DEPENDECIES"
echo "----------------------------------------------------------------------"
sudo apt-get update
sudo apt-get install -y git make gcc linux-headers-$(uname -r) xenstore-utils
sudo apt install python -y #just for debugging a little


echo "----------------------------------------------------------------------"
echo "   DomU >> CLONING MAGICWAND REPOS"
echo "----------------------------------------------------------------------"
mkdir -p ${PRODUCTION_DIR}
cd ${PRODUCTION_DIR}
if [ -d "magicwand" ]; then
	cd magicwand; git pull; cd - > /dev/null
else
	git clone https://${GITHUB_TOKEN}@github.com/invincealabs/magicwand.git
fi
if [ -d "magicwand-appinstrumentation" ]; then
	cd magicwand-appinstrumentation; git pull; cd - > /dev/null
else
	git clone https://${GITHUB_TOKEN}@github.com/invincealabs/magicwand-appinstrumentation.git
fi
if [ -d "magicwand-modeling" ]; then
	cd magicwand-modeling; git pull; cd - > /dev/null
else
	git clone https://${GITHUB_TOKEN}@github.com/invincealabs/magicwand-modeling.git
fi
if [ -d "magicwand-commsbackbone" ]; then
	cd magicwand-commsbackbone; git pull; cd - > /dev/null
else
	git clone https://${GITHUB_TOKEN}@github.com/invincealabs/magicwand-commsbackbone.git
fi


echo "----------------------------------------------------------------------"
echo "   DomU >> PREPPING THE ENVIRONMENT"
echo "----------------------------------------------------------------------"
cd ~

cd ~/${PRODUCTION_DIR}/magicwand-commsbackbone/util
echo "./mw_prep"
./mw_prep
cd ~/${PRODUCTION_DIR}/magicwand-commsbackbone/exports/scripts/
echo "./build_pvm_tools"
./build_pvm_tools.sh
# cd ~/${PRODUCTION_DIR}/magicwand-commsbackbone/protvm/kernel/mwcomms/
# make -C /lib/modules/4.4.0-75-generic/build M=$(pwd)
# cd ~/${PRODUCTION_DIR}/magicwand-commsbackbone/protvm/user/wrapper/
# make

echo "  End DomU script part 1"