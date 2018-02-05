#!/bin/bash
set -e

#this will be the dir that contains the various git repos:
PRODUCTION_DIR=ins-production
XEN_PVM_NAME=pvmxen
UBUNTU_IMAGE_URL=http://releases.ubuntu.com/16.04.3/ubuntu-16.04.3-server-amd64.iso
USER_NAME=$(whoami)
read -r -p "Enter password for ${USER_NAME}: " DOMU_PASSWORD
# DOMU_PASSWORD=invincea

echo ${DOMU_PASSWORD} | sudo -S echo sudo enabled

echo "----------------------------------------------------------------------"
echo "   EXAMINING ENVIRONMENT"
echo "----------------------------------------------------------------------"
# Note that we set certain variables to true or false based on user input, but for now this is redundant because we require them all to be true.

if sudo xl list | grep -E ^${XEN_PVM_NAME} ; then
	echo ""
    read -r -p "Domain ${XEN_PVM_NAME} already exists. Destroy it? [y/N] " response
	if [[ "$response" =~ ^([yY][eE][sS]|[yY])+$ ]]; then
		PREP_DESTROY_DOMAIN=true
	else
		echo "Using existing domain..."
		echo "...not actually though, because this isn't implemented and it's not clear whether it would be useful."
		exit
	fi
fi

if ! [ -z "$MWROOT" ]; then
	if [ "$MWROOT" != "~/${PRODUCTION_DIR}/magicwand-commsbackbone" ] ; then
		echo ""
		echo "MWROOT is currently set to:"
		echo "  $MWROOT"
		echo "Continuing will temporarily change it to:"
		echo "  ~/${PRODUCTION_DIR}/magicwand-commsbackbone"
		read -r -p "Ok? [y/N] " response
		if [[ "$response" =~ ^([yY][eE][sS]|[yY])+$ ]]; then
			PREP_SET_MWROOT=true
		else
			echo "Cannot proceed. Exiting."
		    exit
		fi
	fi
else
	PREP_SET_MWROOT=true
fi

if [ -e "/etc/xen/${XEN_PVM_NAME}.cfg" ] ; then
	echo ""
	echo "Config already exists:"
	echo "  /etc/xen/${XEN_PVM_NAME}.cfg"
	read -r -p "Continuing will delete it. Ok? [y/N] " response
	if [[ "$response" =~ ^([yY][eE][sS]|[yY])+$ ]]; then
		PREP_DELETE_XENCFG=true
	else
		echo "Cannot proceed. Exiting."
	    exit
	fi
fi

if [ -e "$(pwd)/${XEN_PVM_NAME}/domains/${XEN_PVM_NAME}/disk.img" -o \
     -e "$(pwd)/${XEN_PVM_NAME}/domains/${XEN_PVM_NAME}/swap.img" ] ; then
	echo ""
	read -r -p "disk.img and/or swap.img exist. Continuing will delete them. Ok? [y/N] " response
	if [[ "$response" =~ ^([yY][eE][sS]|[yY])+$ ]]; then
		PREP_DELETE_IMGS=true
	else
		echo "Cannot proceed. Exiting."
	    exit
	fi
fi

#todo...fix this. actually we'll probably just use keys eventually
# read -r -p "Would you like to choose a custom root password? (default is 'password') [y/N] " response
# if [[ "$response" =~ ^([yY][eE][sS]|[yY])+$ ]]; then
# 	read -r -p "Enter password of choice: " DOMU_PASSWORD
# else
# 	DOMU_PASSWORD=password
# fi

# Create home dirs. chown/chgrp if dirs already exist:
# if [ ! -d "/home/${USER_NAME}" ]; then
# 	echo "Creating home directories..."
# 	sudo mkdir /home/${USER_NAME}
# 	cd /home
# 	sudo chown -R ${USER_NAME} ${USER_NAME}
# 	sudo chgrp -R ${USER_NAME} ${USER_NAME}
# 	cd ${USER_NAME}
# fi

# Set the GITHUB_TOKEN in bashrc:
if [ -z "$GITHUB_TOKEN" ]; then
    echo "Enter GITHUB_TOKEN:"
    read NEW_TOKEN
    read -r -p "Save token to .bashrc for later? [Y/n] " response
	if ! [[ "$response" =~ ^([Nn][Oo]|[Nn])+$ ]]; then
		echo response: $response
	    sudo echo "export GITHUB_TOKEN=${NEW_TOKEN}" >> ~/.bashrc
	    echo "Token written to ~/.bashrc"
	fi
    export GITHUB_TOKEN=${NEW_TOKEN}
fi  
echo "Using GITHUB_TOKEN=${GITHUB_TOKEN}"

echo "----------------------------------------------------------------------"
echo "   DOWNLOADING UBUNTU IMAGE"
echo "----------------------------------------------------------------------"
cd ~

UBUNTU_IMAGE=$(basename http://releases.ubuntu.com/16.04.3/ubuntu-16.04.3-server-amd64.iso)

if [ -d "~/isos/${UBUNTU_IMAGE}" ]; then
	mkdir -p ~/isos
	echo wget -O ~/isos/${UBUNTU_IMAGE} ${UBUNTU_IMAGE_URL}
	wget -O ~/isos/${UBUNTU_IMAGE} ${UBUNTU_IMAGE_URL}
else
	echo "~/isos/${UBUNTU_IMAGE} already exists"
fi

echo "----------------------------------------------------------------------"
echo "   CLONING MAGICWAND REPOS"
echo "----------------------------------------------------------------------"
cd ~

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
	# cd magicwand-commsbackbone; git checkout invincea_integration; git pull; cd - > /dev/null
	cd magicwand-commsbackbone; git pull; cd - > /dev/null
else
	# git clone -b invincea_integration https://${GITHUB_TOKEN}@github.com/invincealabs/magicwand-commsbackbone.git
	git clone https://${GITHUB_TOKEN}@github.com/invincealabs/magicwand-commsbackbone.git
fi


echo "----------------------------------------------------------------------"
echo "   PREPPING THE ENVIRONMENT"
echo "----------------------------------------------------------------------"
cd ~

if [ $PREP_DESTROY_DOMAIN ] ; then
    echo "Destroying ${XEN_PVM_NAME}..."
    sudo xl destroy ${XEN_PVM_NAME}
fi

if [ $PREP_SET_MWROOT ] ; then
	# sed -i "/^export MWROOT=.*/d" ~/.bashrc
	echo "Setting MWROOT..."
	export MWROOT=~/${PRODUCTION_DIR}/magicwand-commsbackbone #this line a prereq for ./mw_prep
fi

if [ $PREP_DELETE_XENCFG ] ; then
    echo "Deleting /etc/xen/${XEN_PVM_NAME}.cfg..."
   	sudo rm /etc/xen/${XEN_PVM_NAME}.cfg
fi

if [ $PREP_DELETE_IMGS ] ; then
	if [ -e "$(pwd)/${XEN_PVM_NAME}/domains/${XEN_PVM_NAME}/disk.img" ] ; then
		echo "Deleting $(pwd)/${XEN_PVM_NAME}/domains/${XEN_PVM_NAME}/disk.img ..."
		sudo rm $(pwd)/${XEN_PVM_NAME}/domains/${XEN_PVM_NAME}/disk.img
	fi
	if [ -e "$(pwd)/${XEN_PVM_NAME}/domains/${XEN_PVM_NAME}/swap.img" ] ; then
		echo "Deleting $(pwd)/${XEN_PVM_NAME}/domains/${XEN_PVM_NAME}/swap.img ..."
		sudo rm $(pwd)/${XEN_PVM_NAME}/domains/${XEN_PVM_NAME}/swap.img
	fi
fi

echo "Deleting INS domains..."
sudo xl list | grep -E ^mw-ins-rump | cut -d \  -f1 | xargs -I {} echo sudo xl destroy {}
sudo xl list | grep -E ^mw-ins-rump | cut -d \  -f1 | xargs -I {} sudo xl destroy {}

cd ${PRODUCTION_DIR}/magicwand-commsbackbone/util
./mw_prep


echo "----------------------------------------------------------------------"
echo "   INSTALLING PACKAGE DEPENDECIES"
echo "----------------------------------------------------------------------"
#todo: uncomment next 3 lines
sudo apt-get update
sudo apt-get install -y python-pip zlib1g-dev libxen-dev xen-tools liblog-message-perl sshpass rsync
sudo pip install python-iptables

echo "----------------------------------------------------------------------"
echo "   BUILDING CUSTOMIZED RUMP PLATFORM"
echo "----------------------------------------------------------------------"

if [ -e "/usr/bin/rumprun" ] ; then
	echo "Deleting /usr/bin/rumprun ..."
	sudo rm /usr/bin/rumprun
fi

cd ~/${PRODUCTION_DIR}/magicwand-commsbackbone/ins/ins-rump
. ./RUMP_ENV.sh
dbgbuildrump

sudo chmod +x ~/ins-production/magicwand-commsbackbone/ins/ins-rump/app-tools/rumprun
sudo cp ~/ins-production/magicwand-commsbackbone/ins/ins-rump/app-tools/rumprun /usr/bin/

echo "----------------------------------------------------------------------"
echo "   BUILDING UNIKERNEL"
echo "----------------------------------------------------------------------"
cd apps/ins-app
make

# rumprun -S xen -di -M 256 -N mw-ins-rump -I xen0,xenif -W xen0,inet,dhcp ins-rump.run
# rumprun -S xen -di -M 256 -N mw-ins-rump -I xen0,xenif -W xen0,inet,static,$RUMP_IP/24,$_GW ins-rump.run

# export GITHUB_TOKEN=06d69e38054c15e99d8d2dc6c3c8d36574e48ddc

#todo: get or create cfg

echo "----------------------------------------------------------------------"
echo "   CREATING XEN IMAGE"
echo "----------------------------------------------------------------------"
cd ~
# echo sudo xen-create-image --config=$(pwd)/${XEN_PVM_NAME}.cfg --hostname=${XEN_PVM_NAME} --dir=$(pwd)/${XEN_PVM_NAME}/ --password="${DOMU_PASSWORD}"
# sudo xen-create-image --config=$(pwd)/${XEN_PVM_NAME}.cfg --hostname=${XEN_PVM_NAME} --dir=$(pwd)/${XEN_PVM_NAME}/ --password="${DOMU_PASSWORD}"
echo sudo xen-create-image --config=$(pwd)/${XEN_PVM_NAME}.cfg --hostname=${XEN_PVM_NAME} --dir=$(pwd)/${XEN_PVM_NAME}/ --accounts
sudo xen-create-image --config=$(pwd)/${XEN_PVM_NAME}.cfg --hostname=${XEN_PVM_NAME} --dir=$(pwd)/${XEN_PVM_NAME}/ --accounts


echo "----------------------------------------------------------------------"
echo "   PUSHING DOMU SCRIPT"
echo "----------------------------------------------------------------------"

#todo: find the proper way to do this
# echo "login:    root"
# echo "password: password"
# echo "command:  "
# echo "sed -i \"s/^PermitRootLogin.*/PermitRootLogin yes/g\" /etc/ssh/sshd_config; service ssh restart; apt-get install rsync; exit"
# echo ""
# echo "then hit Ctrl+5"
# sudo xl console ${XEN_PVM_NAME}

#allow sshd to restart:
# sleep 2 (never mind, installing rsync will cause enough delay)

echo "Clearing old key for ${XEN_PVM_NAME} in .ssh/known_hosts..."
# ssh-keygen -f "/home/twosix/.ssh/known_hosts" -R ${XEN_PVM_NAME}
rm .ssh/known_hosts
echo "Adding new key for ${XEN_PVM_NAME} in .ssh/known_hosts..."
sleep 2
while [ -z "$(ssh-keyscan -H ${XEN_PVM_NAME})" ] ; do
	echo "  waiting for keyscan..."
	sleep 2
done
ssh-keyscan -H ${XEN_PVM_NAME}
ssh-keyscan -H ${XEN_PVM_NAME} >> ~/.ssh/known_hosts

# echo EXITING PREMATURELY
# exit 0

# sshpass -p ${DOMU_PASSWORD} ssh -o StrictHostKeyChecking=no ${USER_NAME}@${XEN_PVM_NAME} ''

echo "Uploading Parts I, II, & III of INS script to DomU..."
echo sshpass -p ${DOMU_PASSWORD} scp -o StrictHostKeyChecking=no ins-domu-1.sh ${USER_NAME}@${XEN_PVM_NAME}:/tmp
sshpass -p ${DOMU_PASSWORD} scp -o StrictHostKeyChecking=no ins-domu-1.sh ${USER_NAME}@${XEN_PVM_NAME}:/tmp
echo sshpass -p ${DOMU_PASSWORD} scp -o StrictHostKeyChecking=no ins-domu-2.sh ${USER_NAME}@${XEN_PVM_NAME}:/tmp
sshpass -p ${DOMU_PASSWORD} scp -o StrictHostKeyChecking=no ins-domu-2.sh ${USER_NAME}@${XEN_PVM_NAME}:/tmp
echo sshpass -p ${DOMU_PASSWORD} scp -o StrictHostKeyChecking=no ins-domu-3.sh ${USER_NAME}@${XEN_PVM_NAME}:/tmp
sshpass -p ${DOMU_PASSWORD} scp -o StrictHostKeyChecking=no ins-domu-3.sh ${USER_NAME}@${XEN_PVM_NAME}:/tmp

# ----------------------------------------------------------------------
#      Part 1 of DomU INS script (:
# ----------------------------------------------------------------------

echo sshpass -p ${DOMU_PASSWORD} ssh -o StrictHostKeyChecking=no ${USER_NAME}@${XEN_PVM_NAME} 'PRODUCTION_DIR='${PRODUCTION_DIR}' GITHUB_TOKEN='${GITHUB_TOKEN}' DOMU_PASSWORD='${DOMU_PASSWORD}' XEN_PVM_NAME='${XEN_PVM_NAME}' USER_NAME='${USER_NAME}' bash /tmp/ins-domu-1.sh'
sshpass -p ${DOMU_PASSWORD} ssh -o StrictHostKeyChecking=no ${USER_NAME}@${XEN_PVM_NAME} 'PRODUCTION_DIR='${PRODUCTION_DIR}' GITHUB_TOKEN='${GITHUB_TOKEN}' DOMU_PASSWORD='${DOMU_PASSWORD}' XEN_PVM_NAME='${XEN_PVM_NAME}' USER_NAME='${USER_NAME}' bash /tmp/ins-domu-1.sh'

echo "----------------------------------------------------------------------"
echo "   SETTING UP Dom0 XENSTORE"
echo "----------------------------------------------------------------------"

cd ~/${PRODUCTION_DIR}/magicwand-commsbackbone/exports/scripts
./mw_init.sh


# ----------------------------------------------------------------------
#      Part 2 of DomU INS script (Inserting Kernel Module):
# ----------------------------------------------------------------------

echo sshpass -p ${DOMU_PASSWORD} ssh -o StrictHostKeyChecking=no ${USER_NAME}@${XEN_PVM_NAME} 'PRODUCTION_DIR='${PRODUCTION_DIR}' GITHUB_TOKEN='${GITHUB_TOKEN}' DOMU_PASSWORD='${DOMU_PASSWORD}' XEN_PVM_NAME='${XEN_PVM_NAME}' USER_NAME='${USER_NAME}' bash /tmp/ins-domu-2.sh'
sshpass -p ${DOMU_PASSWORD} ssh -o StrictHostKeyChecking=no ${USER_NAME}@${XEN_PVM_NAME} 'PRODUCTION_DIR='${PRODUCTION_DIR}' GITHUB_TOKEN='${GITHUB_TOKEN}' DOMU_PASSWORD='${DOMU_PASSWORD}' XEN_PVM_NAME='${XEN_PVM_NAME}' USER_NAME='${USER_NAME}' bash /tmp/ins-domu-2.sh'



echo "----------------------------------------------------------------------"
echo "   INSTANTIATING INS"
echo "----------------------------------------------------------------------"

chmod +x ~/${PRODUCTION_DIR}/magicwand-commsbackbone/ins/ins-rump/app-tools/rumprun
cd ~/${PRODUCTION_DIR}/magicwand-commsbackbone/util
cp ~/${PRODUCTION_DIR}/magicwand-commsbackbone/ins/ins-rump/apps/ins-app/ins-rump.run .

#todo: change 0000 to some number
echo sudo rumprun -S xen -d -M 256 -N mw-ins-rump-0000 -I xen0,xenif,mac=00:16:3e:28:2a:58 -W xen0,inet,dhcp ins-rump.run
sudo rumprun -S xen -d -M 256 -N mw-ins-rump-0000 -I xen0,xenif,mac=00:16:3e:28:2a:58 -W xen0,inet,dhcp ins-rump.run

# sudo XD3_INS_RUN_FILE=/home/twosix/ins-production/magicwand-commsbackbone/ins/ins-rump/apps/ins-app/ins-rump.run python ./mw_distro_ins.py

screen -ls | grep -P "\t\d+\.ins-mgmt" | cut -d$'\t' -f2 | xargs -I {} echo screen -X -S {} quit
screen -ls | grep -P "\t\d+\.ins-mgmt" | cut -d$'\t' -f2 | xargs -I {} screen -X -S {} quit


# echo screen -S ins-mgmt -d -m
# screen -S ins-mgmt -d -m
# echo screen -S ins-mgmt -X stuff "echo "${DOMU_PASSWORD}" | sudo -S echo sudo enabled\n"
# screen -S ins-mgmt -X stuff "echo "${DOMU_PASSWORD}" | sudo -S echo sudo enabled\n"
# echo screen -S ins-mgmt -X stuff "sudo XD3_INS_RUN_FILE=/home/twosix/"${PRODUCTION_DIR}"/magicwand-commsbane/ins/ins-rump/apps/ins-app/ins-rump.run python ./mw_distro_ins.py\n"
# screen -S ins-mgmt -X stuff "sudo XD3_INS_RUN_FILE=/home/twosix/"${PRODUCTION_DIR}"/magicwand-commsbane/ins/ins-rump/apps/ins-app/ins-rump.run python ./mw_distro_ins.py\n"

# # ----------------------------------------------------------------------
# #      Part 3 of DomU INS script (Launching Apache):
# # ----------------------------------------------------------------------

echo sshpass -p ${DOMU_PASSWORD} ssh -o StrictHostKeyChecking=no ${USER_NAME}@${XEN_PVM_NAME} 'PRODUCTION_DIR='${PRODUCTION_DIR}' GITHUB_TOKEN='${GITHUB_TOKEN}' DOMU_PASSWORD='${DOMU_PASSWORD}' XEN_PVM_NAME='${XEN_PVM_NAME}' USER_NAME='${USER_NAME}' bash /tmp/ins-domu-3.sh'
sshpass -p ${DOMU_PASSWORD} ssh -o StrictHostKeyChecking=no ${USER_NAME}@${XEN_PVM_NAME} 'PRODUCTION_DIR='${PRODUCTION_DIR}' GITHUB_TOKEN='${GITHUB_TOKEN}' DOMU_PASSWORD='${DOMU_PASSWORD}' XEN_PVM_NAME='${XEN_PVM_NAME}' USER_NAME='${USER_NAME}' bash /tmp/ins-domu-3.sh'



# echo "----------------------------------------------------------------------"
# echo "   TESTING APACHE"
# echo "----------------------------------------------------------------------"

# echo "curl pvmxen > /dev/null"
# curl pvmxen > /dev/null
# echo ""
# echo "1"
# INS_IP_ADDRESS_LINE=$(sudo xenstore-ls /mw | grep -oP " ip_addrs = \" \d+\.\d+\.\d+\.\d+ ")
# echo "2"
# if [ -z INS_IP_ADDRESS ]; then
# 	echo "No INS IP Address found in XenStore"
# else
# 	echo "3"
# 	INS_IP_ADDRESS=$(echo $INS_IP_ADDRESS_LINE | grep -oP "\d+\.\d+\.\d+\.\d+")
# 	echo "4"
# 	echo "curl "${INS_IP_ADDRESS}" > /dev/null"
# 	curl ${INS_IP_ADDRESS} > /dev/null
# fi  