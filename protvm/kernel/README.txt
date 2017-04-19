# This file gives instructions for compiling, linking and executing the LKM, gnt_srvr.
# Target system:  Ubuntu 14.04 LTS Server (out of the box)

#
# Basic Setup:
#

0> The latest driver is mwcomms. The older driver, char_driver, is
   available for reference but will likely not work with the PVM shim
   and/or the INS application.

1> Copy the driver and the user-mode shim to the PVM. If your system
   is configured correctly this can be done by util/mw_copy_to_pvm

#
# Compilation - on the PVM:
#
1> On the PVM, change directory to ~/protvm/kernel/mwcomms

2> Build the driver; you must have gcc and the kernel header files
   installed:

   # make clean all

3> Change directory to ~/protvm/user/wrapper

4> Build the shim

   # make clean all

5> Copy the shim, tcp_ip_wrapper.so, to the location where your target
   application, e.g. Apache, is configured to look for it.


#
# Load/manage the system:
#

1> Load the mwcomms driver, as root.

   # insmod mwcomms.ko

  You can watch its output if desired, with

   # tail -f /var/log/kern.log

2> Launch the INS application on Dom0, e.g. in
   ins/ins-rump/apps/ins-app run:

   # ./run.sh

3> Launch your TCP/IP application such that LD_PRELOAD references the
   shim. All TCP/IP API calls will be routed from the PVM to the INS
   over the Xen ring buffer and over the INS's network
   stack. Depending on the permissions on the driver's device, you
   might need to be root to launch the application.

4> When the application is done and has exited, you may unload the
   driver, as root:

   # rmmod mwcomms

   You may also destroy the INS VM on dom0:

   # xl destroy <INS VM name>
