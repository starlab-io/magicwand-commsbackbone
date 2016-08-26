#######################################################################################
#
# Instructions for installing Rump unikernels.
#
#######################################################################################

##################################
# Part 1. Xen
##################################

1> Reference URL:  http://www.skjegstad.com/blog/2015/01/19/mirageos-xen-virtualbox/

2> Follow the instructions up to- but not including- the section, 
   "Installing MirageOS in dom0"

	a> In the file, /etc/network/interfaces, replace br0 with xenbr0, since bridging is being done relative to Xen Dom0.

3> The nested virtualization environment built in the instructions has been replicated/verified locally.

##################################
# Part 2. Rump
##################################

1> Reference URL:  https://github.com/rumpkernel/wiki/wiki/Tutorial:-Building-Rumprun-Unikernels

2> Follow the instructions provided in Section 1:  "Building the Rumprun Platform"

3> Relative to the user home directory, execute the following on the command line:

	$> git clone http://repo.rumpkernel.org/rumprun

	$> cd rumprun

	$> git submodule update --init

	$> CC=cc ./build-rr.sh xen

	#
	# Should see the compiler spelled out as cc: x86_64-rumprun-netbsd-gcc at the end of of this 
	# step, if Xen/Dom0 is on a Linux Server 14.04 LTS x86_64 platform.
	#
	$> export PATH="${PATH}:$(pwd)/rumprun/bin"

	#
	# It might be necessary to install the Xen headers explicitly  
	#
	$> sudo apt-get install libxen-dev

##################################
# Part 3. Building applications
##################################

1> Reference URL:  https://github.com/rumpkernel/wiki/wiki/Tutorial:-Building-Rumprun-Unikernels

2> Follow the instructions provided in Section 2:  "Building Applications"

3> WLOG the assumption is made that the application is named test.c and resides in ~/rumprun/test

	$> cd test

	#
	# Compile 
	#
	$> x86_64-rumprun-netbsd-gcc -o test-rumprun test.c  

	#
	# Link 
	#
	$> rumprun-bake xen-pv test-rumprun.bin test-rumprun

	#
	# Start the Unikernel
	# The IP address assigned is aribitrary in its last octet
	#
	$> sudo ../rumprun/bin/rumprun xen -di -I xen0,xenif -W xen0,inet,static,192.168.56.22/24 -- ./test-rumprun.bin
 

