echo "----------------------------------------------------------------------"
echo "     DomU >> RUNNING DOMU SCRIPT"
echo "----------------------------------------------------------------------"


#!/bin/bash

#this will be the dir that contains the various git repos:
PRODUCTION_DIR=ins-production
USER_NAME=$(whoami)

echo "----------------------------------------------------------------------"
echo "     DomU >> EXAMINING ENVIRONMENT, CREATING HOME DIRS"
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
echo "     DomU >> INSTALLING PACKAGE DEPENDECIES"
echo "----------------------------------------------------------------------"
#todo: uncomment next 2 lines
sudo apt-get update
sudo apt-get install -y git make gcc linux-headers-4.4.0-75-generic xenstore-utils


echo "----------------------------------------------------------------------"
echo "     DomU >> CLONING MAGICWAND REPOS"
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
	git clone -b invincea_integration https://${GITHUB_TOKEN}@github.com/invincealabs/magicwand-commsbackbone.git
fi


echo "----------------------------------------------------------------------"
echo "     DomU >> PREPPING THE ENVIRONMENT"
echo "----------------------------------------------------------------------"
cd ~

# if [ $PREP_DESTROY_DOMAIN ] ; then
#     echo "Destroying ${XEN_PVM_NAME}..."
#     sudo xl destroy ${XEN_PVM_NAME}
# fi

# if [ $PREP_DELETE_XENCFG ] ; then
#     echo "Deleting /etc/xen/${XEN_PVM_NAME}.cfg..."
#    	sudo rm /etc/xen/${XEN_PVM_NAME}.cfg
# fi

# if [ $PREP_DELETE_IMGS ] ; then
# 	if [ -e "$(pwd)/${XEN_PVM_NAME}/domains/${XEN_PVM_NAME}/disk.img" ] ; then
# 		echo "Deleting $(pwd)/${XEN_PVM_NAME}/domains/${XEN_PVM_NAME}/disk.img ..."
# 		sudo rm $(pwd)/${XEN_PVM_NAME}/domains/${XEN_PVM_NAME}/disk.img
# 	fi
# 	if [ -e "$(pwd)/${XEN_PVM_NAME}/domains/${XEN_PVM_NAME}/swap.img" ] ; then
# 		echo "Deleting $(pwd)/${XEN_PVM_NAME}/domains/${XEN_PVM_NAME}/swap.img ..."
# 		sudo rm $(pwd)/${XEN_PVM_NAME}/domains/${XEN_PVM_NAME}/swap.img
# 	fi
# fi

cd ~/${PRODUCTION_DIR}/magicwand-commsbackbone/util
echo "./mw_prep"
./mw_prep
cd ~/${PRODUCTION_DIR}/magicwand-commsbackbone/exports/scripts/
echo "./build_pvm_tools"
./build_pvm_tools.sh
# cd ${PRODUCTION_DIR}/magicwand-commsbackbone/protvm/kernel/mwcomms/
# make -C /lib/modules/4.4.0-75-generic/build M=$(pwd)/modules
# cd ${PRODUCTION_DIR}/magicwand-commsbackbone/protvm/user/wrapper/
# make
# cd ${PRODUCTION_DIR}/magicwand-commsbackbone/protvm/kernel/mwcomms/
# echo "sudo insmod mwcomms.ko"
# sudo insmod mwcomms.ko


# sudo echo xenfs /proc/xen xenfs defaults 0 0 >> /etc/fstab
# cat /etc/fstab
# sudo mount -a

# echo "./mw_init"
# sudo MWROOT=$MWROOT ./mw_init.sh

# echo "----------------------------------------------------------------------"
# echo "     BUILDING CUSTOMIZED RUMP PLATFORM"
# echo "----------------------------------------------------------------------"
# cd ../ins/ins-rump
# . ./RUMP_ENV.sh
# dbgbuildrump

# echo "----------------------------------------------------------------------"
# echo "     BUILDING UNIKERNEL"
# echo "----------------------------------------------------------------------"
# cd apps/ins-app
# make

# # rumprun -S xen -di -M 256 -N mw-ins-rump -I xen0,xenif -W xen0,inet,dhcp ins-rump.run
# # rumprun -S xen -di -M 256 -N mw-ins-rump -I xen0,xenif -W xen0,inet,static,$RUMP_IP/24,$_GW ins-rump.run

# # export GITHUB_TOKEN=06d69e38054c15e99d8d2dc6c3c8d36574e48ddc

# #todo: get or create cfg

# echo "----------------------------------------------------------------------"
# echo "     CREATING XEN IMAGE"
# echo "----------------------------------------------------------------------"
# cd ~
# echo sudo xen-create-image --config=$(pwd)/${XEN_PVM_NAME}.cfg --hostname=${XEN_PVM_NAME} --dir=$(pwd)/${XEN_PVM_NAME}/ --password="password"
# sudo xen-create-image --config=$(pwd)/${XEN_PVM_NAME}.cfg --hostname=${XEN_PVM_NAME} --dir=$(pwd)/${XEN_PVM_NAME}/ --password="password"


# echo "----------------------------------------------------------------------"
# echo "     ADDING DOMU SCRIPT"
# echo "----------------------------------------------------------------------"

# #todo: find the proper way to do this
# echo "login:    root"
# echo "password: password"
# echo "command:  sed -i \"s/^PermitRootLogin.*/PermitRootLogin yes/g\" /etc/ssh/sshd_config; service ssh restart; exit"
# echo "then hit Ctrl+5"

# sudo xl console ${XEN_PVM_NAME}
# sshpass -p password rsync -Pav ins-domu.sh root@pvmxen:~
# sshpass -p password ssh root@pvmxen 'bash ins-domu.sh'

