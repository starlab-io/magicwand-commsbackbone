
Architecture
============

![MWcomms Architecture diagram](ins_diagram.png)

## Overview

The purpose of the MAGICWAND MwComms network isolation channel is to provide the MAGICWAND detection and mitigation engine with comprehensive telemetry data on all TCP/IP connections to the protected process running on the protected virtual machine.  This is accomplished by intercepting all syscalls pertaining to IPv4 sockets made by the protected process by means of a preloaded shared library shim between the protected process and system libraries. A list of functions that are intercepted by the shared library shim are provided in table 4.1 below:

|          |         |          |             |
|:---------|---------|----------|:------------|
| write    | socket  | connect  | getsockopt  |
| read     | bind    | send     | setsockopt  |
| readv    | listen  | sendto   | getsockname |
| writev   | accept  | recv     | getpeername |
| close    | accept4 | recvfrom | fcntl       |
| shutdown |         |          |             |
Table 4.1



## Protected Virtual machine

![MWcomms Architecture diagram](pvm_diagram.png){#id .class width=250}

The protected virtual machine is an unprivileged domain running on top of the Xen hypervisor.  The PVM hosts the protected application, the shared library shim as well as the PVM MwComms driver code which is responsible for coordinating all syscalls, sending them over the Xen ring buffer, and providing the MAGICWAND detection and mitigation engine with telemetry data regarding the state of the protected application's network stack.

### Shim
The purpose of the shim is to transparently bypass the operating system network stack in favor of a direct shared-memory connection to the associated unikernel network stack.  This is done by intercepting the system calls in Table 1.4 and packing them into a MwComms message structure and writing it to the MwComms driver or by calling an ioctl to set socket parameters.  In the case of a system call needing to perform an operation on a local file descriptor or a local socket of a type other than IPv4 the calls are forwarded to the correct libraries for the operating system to handle as normal.

### PVM Kernel MwComms Kernel Module

The PVM kernel module contains the majority of the logic that maintains the sate of the MwComms network isolation channel. It is responsible for receiving and forwarding all requests and responses between the unikernel and the protected process.  As well as allocating and publishing the shared memory grant refs to the Xenstore, and establishing the event channel.


![Xen Component Diagram](xen_diagram.png){#id .class width=150 height=300px }


## INS

![INS Architecture diagram](rumprun_diagram.png){#id .class width=200 }


### Rumprun Unikernel

The Rumprun unikernel is a minimally modified NetBSD rump kernel that is able to run unmodified POSIX code as a unikernel.  The Rumprun unikernel was chosen over other unikernels to act as the isolated network stack because, as a general purpose unikernel, it has most operating system components already built in, allowing development work to focus on core application infrastructure instead of writing drivers that already exist. The unikernel component of the MwComms channel is where the commands that have been forwarded over the ring buffer are executed and responses returned.  The INS also has a driver component that allows it to interface with the Xen ring buffer.

### Event Dispatcher

This is where the messages are received from the xe device, converted to their NetBSD equivalents and executed.  Once a command is processed, or a function returned, it is converted back into an MWComms message and sent back down the comms channel to to the originating process.

