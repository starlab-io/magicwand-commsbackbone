# This file gives instructions for compiling, linking and executing the LKM, gnt_srvr.
# Target system:  Ubuntu 14.04 LTS Server (out of the box)

#
# Basic Setup:
#
1> Be a user with sudo privileges, not root

2> Create a work directory anywhere, preferably under the 
   user home directory

3> Copy these two files to the directory:
      Makefile
      gnt_srvr.c

#
# Compilation:
#
1> Change directory to the work directory and build: 
   work_dir> make

#
# Load/manage the kernel module:
#

1> Compilation will produce the module, gnt_srvr.ko

2> Load the module:
   work_dir> sudo insmod gnt_srvr.ko

3> To verify the module is loaded:
   work_dir> sudo lsmod | grep gnt_srvr

4> Remove the module later:
   work_dir> sudo rmmod gnt_srvr

5> The module generates a gratuitous amount of logging. 
   To see that in the most convenient way, open 
   another terminal window, and then do as follows:
   some_dir> cd /var/log
   log> tail -f kern.log


