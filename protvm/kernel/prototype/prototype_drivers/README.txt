###################################################################################################################################
#
# This README explains how to use the files in this directory to build the server/client shared memory prototype. It works like
# this. Start the server first, and then start the client.  When the client starts, the unikernels use out-of-band XenStore 
# keys, watches, event queues and the XenBus interface to step through a simple protocol to enable the two domU unikernels to 
# share a page of memory between them. The sharing, in turn,  is accomplished through the use of grant table.  The server uses 
# the gnttab interface to create a new grant entry in the grant table, whereupon it obtains a grant reference for it.  It shares 
# the grant reference with the client, which then uses it and the gntmap interface to map in the shared memory.  
#
# It is important to note the prototype in this directory makes modifications only the mini-os part of the build. It adds a
# front-end driver to both server and client sides. This means it is nearly completely Rump-independent. This makes it really
# easy to port to other unikernels, because they all pretty much all use the mini-os to build on. 
#
# Also, the prototype was built over Xen installed on Linux Server 14.04 LTS.
#
#
###################################################################################################################################

###############################################################
#
# Building and Running the Server/Client unikernels
#
###############################################################


1> The source files, the header file and Makefiles in this directory represent a modified mini-os kernel 
   that Rump wrappers to build unikernels.

	Locations relative to the user home directory:

	1> Source:    ~/rumprun/platform/xen/xen/offer_accept_gnt.c
		* The prototype driver

	2> Makefile:  ~/rumprun/platform/xen/xen/Makefile 
		* Two mods for compliation/linking

	3> Header:    ~/rumprun/platform/xen/xen/include/mini-os/offer_accept_gnt.h
		* Interface for prototype driver (init() and fini() funcs)

	4> Source:    ~/rumprun/test/test_gnttab.c
		* Simple test application compiled into Rump Kernel

	5> Source:    ~/rumprun/platform/xen/librumpnet_xenif/xenif_user.c
		* Hosts init() and fini() functions for offer_accept_gnt module 

2> The source file, offer_accept_gnt.c, enables the production of two rumps, a client and a server.  
   This is done by simply toggling the IS_SERVER variable between 0/1.  0 ==> Client and 1 ==> Server.  
   The server is built first, and the associated unikernel is started first.  
   The client is built/started next. 

3> For test purposes, a simple application named test_gnttab.c is used. It just spins in a while loop.
   It is kept in a directory named ~/rumprun/test

4> To build the server, set IS_SERVER to 1, and then execute:

	$> cd ~/rumprun
	$> CC=cc ./build-rr.sh xen
        $> export PATH="${PATH}:$(pwd)/rumprun/bin"
	$> cd test
	$> x86_64-rumprun-netbsd-gcc -o test_gnttab_srvr-rumprun test_gnttab.c 
	$> rumprun-bake xen-pv test_gnttab_srvr-rumprun.bin test_gnttab_srvr-rumprun

5> To start the server unikernel, remain in the test directory and execute:

	$> sudo ../rumprun/bin/rumprun xen -di -I xen0,xenif -W xen0,inet,static,192.168.56.21/24 -- ./test_gnttab_srvr-rumprun.bin 

6> To build the client, set IS_SERVER to 0, and then execute:

	$> cd ~/rumprun
	$> CC=cc ./build-rr.sh xen
        $> export PATH="${PATH}:$(pwd)/rumprun/bin"
	$> cd test
	$> x86_64-rumprun-netbsd-gcc -o test_gnttab_client-rumprun test_gnttab.c 
	$> rumprun-bake xen-pv test_gnttab_client-rumprun.bin test_gnttab_client-rumprun

7> To start the client unikernel, remain in the test directory and execute:

	$> sudo ../rumprun/bin/rumprun xen -di -I xen0,xenif -W xen0,inet,static,192.168.56.22/24 -- ./test_gnttab_client-rumprun.bin 

###############################################################
#
# Generating XenStore Keys for the Protocol 
#
###############################################################

1> Open a terminal on Dom0 (Linux Server 14.04)

2> Create the node for the out-of-band key family:

	$> sudo xenstore-write /unikernel/random ""

3> Enable the server/client to read/write to/from any keys created under the node:

	$> sudo xenstore-chmod /unikernel/random b

4> Create/initialize the protocol keys: 

	$> sudo xenstore-write /unikernel/random/server_id         0
	$> sudo xenstore-write /unikernel/random/client_id         0
	$> sudo xenstore-write /unikernel/random/gnt_ref           0
	$> sudo xenstore-write /unikernel/random/msg_len           0
	$> sudo xenstore-write /unikernel/random/evt_chn_port      0
	$> sudo xenstore-write /unikernel/random/client_local_port 0


