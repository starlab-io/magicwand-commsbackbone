
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
| INS      | Instrumented Unikernel Network Stacks |

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
This section explains how to setup a basic development environment on which
to build, run and test the mwcomms/ins subsystem.

#### System configuration
    # BIOS in Legacy mode
    # enable VT-d
    # WiFi off

#### Install Ubuntu 16.04 desktop version
    # choose LVM disk layout
    # encrypt disk

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

### Environment Setup After Dom0 Reboot <a name="Section_02"></a>
Once the full environment is setup this section explains what
steps are needed after Dom0 is rebooted.

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
Once the full environment is setup this section explains what
steps are needed after switching to a new mwcomms/ins git branch.

#### Dom0
    # cleanup existing git branch
    $ cd $MWROOT
    $ git checkout -- .
    $ git clean -d -f
    $ git checkout <branch>

#### PVM
    # cleanup existing source code
    $ cd ; rm -rf ins-production

***

### NetFlow Setup and Use <a name="Section_04"></a>
Communication channel and API to the mwcomms driver used to monitor protected
application traffic (syscalls), send requests and receive responses. Requests
can be used to query the driver for information or to specify an action to be
taken, such as a LVDDOS mitigation.

#### API
Communication between the mwcomms driver and the monitoring application uses a
TCP/IP socket, the address and port are published by the mwcomms driver in
XenStore on Dom0 at "/mw/pvm/netflow".

A python library used to interface with Netflow is located at:
* magicwand-commsbackbone/util/mw_netflow.py

A python script used to exercise the Netflow API is located at:
* magicwand-commsbackbone/util/mw_netflow_consumer.py

The core Netflow mwcomms driver files are located at:
* magicwand-commsbackbone/exports/imports/mw_netflow_iface.h
* magicwand-commsbackbone/common/message_types.h
* magicwand-commsbackbone/protvm/kernel/mwcomms/mwcomms-netflow.h
* magicwand-commsbackbone/protvm/kernel/mwcomms/mwcomms-netflow.c
* magicwand-commsbackbone/protvm/kernel/mwcomms/mwcomms-socket.c

#### Traffic Information
As soon as a Netflow connection is established with the mwcomms driver all
protected application traffic monitoring data is sent to the monitoring
application in real time. Multiple connections can be established and traffic
data will be sent to every connected client. Traffic is broken into chunks
equivalent to syscalls (called observations in the code) and defined by
mw_observation_t:

* MwObservationNone    = 0
* MwObservationCreate  = 1
* MwObservationBind    = 2
* MwObservationAccept  = 3
* MwObservationConnect = 4
* MwObservationRecv    = 5
* MwObservationSend    = 6
* MwObservationClose   = 7

Each "observation" message is comprised of a set of data defined by the mw_netflow_info_t structure:

* mw_base_t        base;             // signature: MW_MESSAGE_NETFLOW_INFO
* mw_obs_space_t   obs;              // mw_observation_t
* mw_timestamp_t   ts_session_start; // beginning of session
* mw_timestamp_t   ts_curr;          // time of observation
* mw_socket_fd_t   sockfd;           // Dom-0 unique socket identifier
* mw_endpoint_t    pvm;              // local (PVM) endpoint info
* mw_endpoint_t    remote;           // remote endpoint info
* mw_bytecount_t   bytes_in;         // tot bytes received by the PVM
* mw_bytecount_t   bytes_out;        // tot bytes sent by the PVM
* uint64_t         extra;            // extra data: new sockfd on accept msg

A Request can be sent to the mwcomms driver asking for information or for an
action to be executed, these requests are defined by mt_sockfeat_name_val_t:

* MtSockAttribNone
* MtChannelTrafficMonitorOn
* MtChannelTrafficMonitorOff
* MtSockAttribIsOpen
* MtSockAttribOwnerRunning
* MtSockAttribNonblock
* MtSockAttribReuseaddr
* MtSockAttribReuseport
* MtSockAttribKeepalive
* MtSockAttribDeferAccept
* MtSockAttribNodelay
* MtSockAttribSndBuf
* MtSockAttribRcvBuf
* MtSockAttribSndTimeo
* MtSockAttribRcvTimeo
* MtSockAttribSndLoWat
* MtSockAttribRcvLoWat
* MtSockAttribError
* MtSockAttribGlobalCongctl
* MtSockAttribGlobalDelackTicks

#### Mitigation
A mitigation request is used to address a possible LVDDOS attack. The following
example illustrates how to setup "netcat" as a protected application, make a
connection using another instance of "netcat" then request mwcomms to close
the connection using the Netflow interface.

* A PVM should be running with the mwcomms driver loaded
* An INS instance should also be running and be connected to the mwcomms driver
* On Dom0 execute the script located in `magicwand-commsbackbone/util/mw_netflow_consumer.py`
* On the PVM start "netcat" in server mode, put the following two lines in a file and make it executable.
    * `export LD_PRELOAD=/home/pvm/ins-production/magicwand-commsbackbone/protvm/user/wrapper/tcp_ip_wrapper.so`
    * `netcat -l 4444`
* Execute the script
* On a different machine execute `netcat [IP] 4444` using the INS instance IP address
* Once connected you can type any message into either netcat and it will be displayed on the other
* In the "mw_netflow_consumer.py" window type the letter `c` which will display open sockets
* To shutdown the netcat open socket, select it from the list, you should see the netcat client terminate
* Shutdown the netcat server with Ctrl-C

To test closing of a socket while data is being transferred use a syntax like the following:
* server: `netcat -l 4444 > outfile`
* client: `netcat [IP] 4444 < infile`
Initiate the mitigation while the file is being transferred.

***

### Multiple INS Startup and Control <a name="Section_05"></a>
The mwcomms/ins subsystem allows for multiple INS instances to be running
in parallel. A front end python utility is available for controlling multiple
INS instances along with routing network requests to specific INS instances
to regulate load evenly.

***

### DebugFS Interface <a name="Section_06"></a>
A Linux DebugFS interface is available in the mwcomms driver to allow for
inspecting driver operation with a minimal impact on timing.

The main mwcomms DebugFS code is located at:
* magicwand-commsbackbone/protvm/kernel/mwcomms/mwcomms-debugfs.c
* magicwand-commsbackbone/protvm/kernel/mwcomms/mwcomms-debugfs.h

This interface is disabled by default, it can be enabled by uncommenting
the MW_DEBUGFS line in the Makefile:
* magicwand-commsbackbone/protvm/kernel/mwcomms/Makefile

To use the Request Tracing feature the INS needs to be rebuilt with the MW_DEBUGFS
flag uncommented also:
* magicwand-commsbackbone/ins/ins-rump/apps/ins-app/Makefile

The mwcomms DebugFS directory is located at:
* /sys/kernel/debug/mwcomms

and contains four files:

* message_counts -> (read only) returns a formatted table of all MtRequest and response times as they are read/written from/to the ring buffer

* reset -> (write only) when any value is written to this file, all the counts displayed in message_counts are reset

* tracing_on -> (read/write) if the value is nonzero, tracing is enabled, and the counts per message type should increase as the are written to/read from the ring buffer

* request_trace -> (read only) returns a buffer of transactions processed by the mwcomms driver

A python utility is available to read the request_trace data, filter it and display it in a more readable format:
* magicwand-commsbackbone/protvm/kernel/mwcomms/mw_request_trace.py

***

### Shim Logging <a name="Section_07"></a>
The Shim is an libraray loaded by the APT using the LD_PRELOAD directive and allows the mwcomms driver to
intercept ATP system calls associated with socket communication and forward those to the mwcomms driver for
processing. This subsystem has a debug logging facility that can be enabled through the Makefile and then
data from the Shim and stores it in a logfile.

To enable this feature uncomment the line containing the ENABLE_LOGGING define and rebuild the Shim:
* magicwand-commsbackbone/protvm/user/wrapper/Makefile

The Shim prints messages to the log using the log_write() function call, the verbosity and types of
messages printed can be controlled using the log_level parameter.

By default logfiles are created in the "/tmp/ins_log" directory, specified in the hearder file:
* magicwand-commsbackbone/common/user_common.h

***

### Using GDB with the INS <a name="Section_08"></a>
GDB can be used to attach to the INS and used to debug problems. The easiest way to start the
INS with the proper parameters for using GDB is by using the "run.sh" script and supply the
"-g" paramter:
* magicwand-commsbackbone/ins/ins-rump/apps/ins-app/run.sh

This paramter will also cause the run.sh script to print out the proper GDB command line required
to connect to the INS instance.

***

### Makefile Features <a name="Section_09"></a>
Many debug debug features for the Shim, mwcomms driver and INS can be enabled
by uncommenting defines in the various makefiles. In many cases these debug
features cause messages to be printed and can severly effect the timing and performance
of the mwcomms/ins subsystem.
* magicwand-commsbackbone/ins/ins-rump/apps/ins-app/Makefile.template
* magicwand-commsbackbone/ins/ins-rump/apps/npfctl_test/Makefile.template
* magicwand-commsbackbone/ins/ins-rump/apps/lib/npfctl/Makefile.template
* magicwand-commsbackbone/ins/ins-rump/src-netbsd/sys/rump/dev/lib/libaccf_dataready/Makefile.template
* magicwand-commsbackbone/ins/ins-rump/src-netbsd/sys/rump/dev/lib/libxenevent/Makefile.template
* magicwand-commsbackbone/protvm/kernel/mwcomms/Makefile.template
* magicwand-commsbackbone/protvm/user/wrapper/Makefile.template

***

