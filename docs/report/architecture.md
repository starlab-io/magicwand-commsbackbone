
Architecture
============
***

NOTES:

A higher level technical description of our part of the system. Describe each sub-system
(ATP, shim, mwcomms, ring buffer, ins, xen, VMs, etc), APIs, interfaces, interactions, maybe some
technical details, this section should set the stage for the detailed design. Include a diagram
that has all the major pieces listed, entry points and APIs and lines indicating communication
and interaction.

***

![INS Architecture diagram](report/ins_diagram.png)


This section will give an explanation of the major components of the MAGICWAND-commsbackbone in the same order a request would take through the system.

## Shim 

###### Location:
```
protvm/user/wrapper
```
The wrapper directory contains code that is compiled into a shared object and preloaded by the dynamic linker to intercept the following syscalls.  And forward them to the mwcomms driver.


|          |         |          |             |
|:---------|---------|----------|:------------|
| write    | socket  | connect  | getsockopt  |
| read     | bind    | send     | setsockopt  |
| readv    | listen  | sendto   | getsockname |
| writev   | accept  | recv     | getpeername |
| close    | accept4 | recvfrom | fcntl       |
| shutdown |         |          |             |



In order for an application to take advantage of the INS it must be started with the LD_PRELOAD environment variable set to the location of the tcp_ip_wrapper.so shared library.  When that is done, all syscalls pertaining to sockets, listed in the table above will be converted to a format that the mwcomms driver understands and forwarded to the mwcomms driver where it will then be forwarded over the Xen shared memory ring buffer to the INS.

## Mwcomms driver

The mwcomms driver has the logic to negotiate with the INS to create a shared memory ring buffer, and an event channel to signal when messages are available for consumption by the INS, as well as all logic to keep track of when INS's are created or destroyed, and contains all the code for the backing pseudo file system.  This is perhaps the most complex single component of the entire comms-channel.

## Xenstore

Xenstore is a space similar to procfs designed to store information that can be read by other domains.  MwComms uses it store the grant refs, and other information that the different pieces of the INS need to communicate with one another.


## Xen Ring Buffer

The xen ring buffer is a purely xen component, it is composed of pages of memory that, in our case, are given read and write access by both the protected virtual machine and the rumprun unikernel.  It is in this shared memory space that messages are written and consumed by both the INS and the mwcomms driver.  A second component of the ring buffer is the event channel, which is a channel by which the Rumprun unikernel and the mwcomms driver can notify one another that a message has been written to the ring buffer and is ready to be consumed.

## Rumprun Unikernel

The Rumprun unikernel is a general purpose unikernel that has minimally modified the NetBSD rump kernel, to allow running unmodified POSIX code as a unikernel.  As a general purpose unikernel, the Rumprun unikernel was chosen as it had most operating system components already built in, and could allow us to focus on developing the core application infrastructures instead of worrying about writing drivers that already exist in the case that we need them.

## Rumprun Unikernel xe device

The Rumprun unikernel equivalent to the mwcomms driver that interfaces with the ring buffer and XenStore to coordinate the comms-channel initialization as well as pull messages from the ring buffer and send them to the main INS application code.

## xenevent.c

This is where the messages are received from the xe device, converted to their NetBSD equivalents and executed.  Once a command is processed, or a function returned, it is converted back into an MWComms message and sent back down the comms channel to to the originating process.

