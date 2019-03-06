
Development Environment
=======================

***

This chapter describes in detail how to setup and verify a development
environment for the mwcomms/ins component of Magicwand.

|  Term    | Definition |
|:---------|------------|
| ATP      | Application to protect (ex: Apache) |
| PVM      | Protected Virtual Machine, VM where the ATP runs |
| Shim     | The LD_PRELOAD library loaded by the ATP providing a interface to the mwcomms driver |
| mwcomms  | The Linux Kernel driver running on the PVM which interfaces with the Shim and INS |
| INS      | nstrumented Unikernel Network Stacks |

REMOVE: This section should describe in detail how someone would setup a system with our code and get it all running and how verify it's actually working. I would pull in our "setup" wiki contents at a bare minimum, then add information about netflow, any scripts such as the netflow library and example script, the mw_distro_ins.py script for controlling INS instances, debugging built into the code, like the debugfs or gdb, or shim logging. Point to the Makefiles where we have features commented out. This should be a very practical section, after reading this someone not familiar with the project could setup a development environment, verify everything is working as designed, make code changes to each major sub-system, compile those changes and restart the environment with those changes.

***

### Table of Contents
1.  [Basic Development Environment Setup](#Section_01)
2.  [Environment Setup After Dom0 Reboot](#Section_02)
3.  [Environment Setup After Switching GIT Branches](#Section_03)
4.  [NetFlow Setup and Use](#Section_04)
5.  [Multiple INS Startup and Control](#Section_05)
6.  [DebugFS Interface](#Section_06)
7.  [Shim Logging](#Section_07)
8.  [Using GDB with the INS](#Section_08)
9.  [Makefile Features](#Section_09)

***

### Basic Development Environment Setup <a name="Section_01"></a>

#### System configuration
    - BIOS in Legacy mode
    - enable VT-d
    - WiFi off

#### Install Ubuntu 16.04 desktop version
    - choose LVM disk layout
    - encrypt disk

#### Install latest Linux 4.4.0 kernel

    $ sudo apt-cache search 'linux-image-4.4.0-([1-9]*)-generic'
    $ sudo apt-get install <latest_kernel>
    $ sudo shutdown -r now

    # May have to hit "Esc" during boot to manually select Linux 4.4.0 kernel

#### Install additional software on Dom0
    $ sudo apt-get install openssh-server gcc git zlib1g-dev

#### Install Xen
    $ sudo apt-get install xen-hypervisor-4.6-amd64 xen-tools libxen-dev bison flex

    # reboot system off Xen kernel (this kernel will be the default in grub)
    $ sudo shutdown -r now

    # verify the installation suceeded
    $ sudo xl list

#### Setup bridged networking on Dom0
    # stop and disable Network Manager (systemd method)
    $ sudo systemctl stop network-manager
    $ sudo systemctl disable network-manager

    # setup /etc/network/interfaces file,
    # where <interface> is your wired network interface

    auto lo <interface> xenbr0
    iface lo inet loopback
    
    iface <interface> inet manual
    
    iface xenbr0 inet dhcp
        bridge_ports <interface>

    # reboot system so new network settings take effect
    $ sudo shutdown -r now

    # verify network
    $ brctl show

#### Setup XD3 software on Dom0
    # create SSH key if needed (add to github)
    $ ssh-keygen

    # choose a directory for the project and clone the git repository
    $ mkdir -p /home/gregp/Projects/GIT
    $ cd /home/gregp/Projects/GIT
    $ git clone git@github.com:twosixlabs/magicwand-commsbackbone.git
    $ cd magicwand-commsbackbone
    $ pwd   # this directory is MWROOT

#### Create and boot protected VM (PVM) on Dom0
    # create new Xen instance for PVM
    $ sudo xen-create-image \
          --genpass=0 \
          --password=root \
          --hostname=xd3-pvm \
          --dhcp \
          --dir=$HOME \
          --dist=xenial \
          --vcpus=1 \
          --memory=2048MB \
          --size=20G \
          | tee create-guest.log

    # load the loopback driver (if using --dir option above)
    $ sudo modprobe loop max_loop=255

    # boot the PVM guest
    $ sudo xl create /etc/xen/xd3-pvm.cfg

    # obtain the PVM guest console (login as user "root" with password "root")
    $ sudo xl console xd3-pvm

        # check kernel version (mwcomms driver requires version 4.4.x)
        $ uname -r

        # create non-root user
        $ adduser pvm
        $ usermod -aG sudo pvm

        # upgrade packages
        $ apt-get update
        $ apt-get upgrade
        $ apt-get dist-upgrade

        # install additional software
        $ apt-get install make gcc  apache2 w3m
        $ apt-get install linux-headers-$(uname -r)

        # obtain the PVM guest IP address and gateway
        $ ip addr show eth0 | grep 'inet '   # this is the PVM_IP

        # drop the PVM guest console
        $ Ctrl-]

#### Setup environment on Dom0
    $ ip route | grep default   # this is the _GW
    # add the following environment variables to $HOME/.bashrc
    export MWROOT='/home/gregp/Projects/GIT/magicwand-commsbackbone'
    export PVM_USER='pvm'
    export INS_DIR=$MWROOT'/ins/ins-rump/apps/ins-app'
    export RUMP_IP='192.168.1.100'   # Free IP address for Rump/INS
    export _GW='192.168.1.1'         # IP address of Rump/INS gateway
    export PVM_IP='192.168.1.101'    # IP address of PVM

    # logout and log back in

#### Setup software build environment on Dom0
    $ cd $MWROOT/util

    # copy contents of Makefile.template into Makefile for each project directory
    $ ./mw_prep

    # destroy current INS
    $ sudo xl destroy mw-ins-rump

    # set up the /mw key root in the XenStore
    $ ./mw_setkeys

    # copy mwcomms source code to PVM
    $ ssh-copy-id $PVM_USER@$PVM_IP
    $ ./mw_copy_to_pvm

    # build rumprun (using shell defined function)
    $ cd $MWROOT/ins/ins-rump
    $ source RUMP_ENV.sh
    # both 'dbgbuildrump' and 'dbgrebuildrump' are available
    # 'dbgrebuildrump' attempts to cleanup previous builds
    $ dbgrebuildrump

#### Setup PVM
    $ ssh $PVM_USER@$PVM_IP

    # build shim
    $ cd ~/ins-production/magicwand-commsbackbone/protvm/user/wrapper
    $ make clean ; make

    # add ld preload directive to apache2
    $ sudo vi /etc/apache2/envvars
    export LD_PRELOAD=/home/pvm/ins-production/magicwand-commsbackbone/protvm/user/wrapper/tcp_ip_wrapper.so

    # disable ipv6 in apache2 (add '0.0.0.0:' to each port listed)
    $ sudo vi /etc/apache2/ports.conf
    Listen 0.0.0.0:80
    Listen 0.0.0.0:443

    # build and load mwcomms driver on PVM
    $ cd ~/ins-production/magicwand-commsbackbone/protvm/kernel/mwcomms
    $ make clean ; make
    $ ./load.sh

    # setup Apache2 modules
    $ sudo a2dismod mpm_event
    $ sudo a2enmod mpm_prefork

#### (Optional) Setup PVM to run Xray-Apache in Docker container
    # Note: This Docker container will ONLY run in the TwoSix DMZ
    # Note: PVM should have at least 3GB of memory to build containers

    # remove old Docker packages if present
    $ sudo apt-get remove docker docker-engine docker.io

    # install packages to allow apt to use a repository over HTTPS
    # sudo apt-get update
    $ sudo apt-get install apt-transport-https ca-certificates curl software-properties-common

    # add Docker’s official GPG key
    $ curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -

    # verify key is present (fingerprint: 9DC8 5822 9FC7 DD38 854A E2D8 8D81 803C 0EBF CD88)
    $ sudo apt-key fingerprint 0EBFCD88

    # setup "stable" repository (add "edge" or "test" if desired)
    $ sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"

    $ sudo apt-get install docker.io

    # clone magicwand repos
    $ cd ins-production
    $ git clone git@github.com:twosixlabs/magicwand-appinstrumentation.git
    $ git clone git@github.com:twosixlabs/magicwand.git
    $ cd magicwand-appinstrumentation
    $ git checkout ins_integration

    # build the xray-apache base container
    $ cd images/suts/xray-apache
    $ sudo ./build.sh
    $ cd ../xray-apache-ins
    $ ./build.sh

#### Build and start INS on Dom0
    # ensure any previous INS instance has been destroyed
    # and XenStore is initialized and old keys are cleared
    $ cd $INS_DIR
    $ make clean ; make
    $ ./run.sh

#### Start Apache2 on PVM
    $ sudo apache2ctl start

#### (Optional) Start Xray-Apache container on PVM
    # Note: ONLY start Apache2 on the PVM or in the Docker container, NOT BOTH
    $ cd ~/ins-production/magicwand-appinstrumentation/images/suts/xray-apache-ins
    $ ./run.sh

#### Test setup is working on Dom0
    # navigate to INS IP, should see default Apache2 webpage
    # Dom0 should have /etc/hosts entry for xd3-pvm set to INS ip address
    $ firefox --new-tab $RUMP_IP

    # apache benchmark 'ab' is another good tool to use
    $ ab -n 100 -c 1 http://<RUMP_IP>:80/

### Environment Setup After Switching GIT Branches <a name="Section_02"></a>

#### Dom0
    $ sudo modprobe loop max_loop=255
    $ sudo xl create /etc/xen/xd3-pvm.cfg
    $ cd $MWROOT/util
    $ ./mw_prep
    $ ./mw_setkeys
    $ cd $MWROOT/ins/ins-rump
    $ source RUMP_ENV.sh
    $ dbgrebuildrump

#### PVM
    $ ssh $PVM_USER@$PVM_IP
    $ cd ~/ins-production/magicwand-commsbackbone/protvm/kernel/mwcomms
    $ sudo ./load.sh

#### Dom0
    $ cd $INS_DIR
    $ ./run.sh

#### PVM
    $ sudo apache2ctl start

#### Dom0
    $ firefox --new-tab $RUMP_IP
    # NOTE: To generate more Apache2 load, set the following parameters in Firefox "about:config"
    # browser.cache.memory.enable "false"
    # browser.cache.disk.enable "false"

### Environment Setup After Switching GIT Branches <a name="Section_03"></a>

#### Dom0
    # cleanup existing git branch
    $ cd $MWROOT
    $ git checkout -- .
    $ git clean -d -f
    $ git checkout <branch>

#### PVM
    # cleanup existing source code
    $ cd ; rm -rf ins-production

****

### NetFlow Setup and Use <a name="Section_04"></a>

****

### Multiple INS Startup and Control <a name="Section_05"></a>

****

### DebugFS Interface <a name="Section_06"></a>

****

### Shim Logging <a name="Section_07"></a>

****

### Using GDB with the INS <a name="Section_08"></a>

****

### Makefile Features <a name="Section_09"></a>

****

