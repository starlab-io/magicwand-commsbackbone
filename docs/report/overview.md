
Overview
========

###### Glossary of terms

| Term    | Definition                                                                |
|:--------|:--------------------------------------------------------------------------|
| INS     | Isolated Network Stack                                                    |
| PVM     | Protected virtual machine                                                 |
| shim    | Shared libary used by LD_PRELOAD to intercept all socket related syscalls |
| NetFlow nterface | The interface responsible for reporting all protocol stack information |
| MwComms | MAGICWAND isolated network channel - all components relating to the isolated network stack including shim, PVM driver, NetFlow interface and unikernel |


Executive Summary
-----------------




Introduction
------------
Methods for Automatic Generalized Instrumentation of Components for Wholesale Application and Network Defense (MAGICWAND) is designed to demonstrate a groundbreaking approach to detecting low-volume, distributed denial of service (LVDDoS) attacks delivered as part of a protected operating environment for off-the-shelf applications and services. MAGICWAND was developed to meet the needs of the Technical Area 3 portion of the extreme Distributed Denial of Service (XD3) Broad Agency Announcement.  MAGICWAND can be split into two main components - the mitigation and detection engine developed by Two Six Labs and the Isolated Network Stack (INS) component developed by Star Lab.  The scope of this document is limited to the INS component of MAGICWAND and will not cover other components of the MAGICWAND system, except when referring to the parts of the system designed to interface with the detection and mitigation engine.

Unikernels are an emerging innovation for developing and deploying secure lightweight stacks that contain a minimal set of libraries, services, and code. The Rumprun unikernel is a general purpose unikernel built on top of NetBSD rump kernels that provides high quality componentized drivers, a libc, and tools to build POSIX-like code into a Rumprun unikernel. [1] The researchers at Star Lab chose the Rumprun unikernel because the built in drivers and libc allow development efforts to focus on core functionality instead of writing drivers and infrastructure that already exist while still providing many of the benefits of a unikernel.

The Xen hypervisor is a lightweight and secure virtualization technology that sits between the hardware and the operating system.  It provides strong isolation between virtual machines on the same host as well as provides a high speed shared memory communication channel, event notification channel, and procfs like interface to share data between virtual machines on the same physical host.  Xen is composed of a primary virtual machine domain called Domain 0 or Dom0, all other unprivileged virtual machine hosts are referred to as DomU's.  Domain 0 is the host from which all commands are run to create, start, stop and destroy Xen domains.

At a high-level, the MAGICWAND isolated network channel allows an arbitrary application that may be at risk for LVDDoS attacks to have all network related syscalls intercepted and forwarded to the network stack of one or more hosts. This system is called the MwComms isolated network channel and is composed of two major components: the protected virtual machine (PVM) and the isolated network stacks (INS's). The isolated network channel works by intercepting all IPv4 system calls made by the protected application and forwarding those calls over the Xen shared memory communication channel to one or more INS's where the calls are then translated and executed. Running the protected application in this manner allows for finer grained monitoring of its resource consumption, usage, and retention than could be observed otherwise.


Architecture
============

![MWcomms Architecture diagram](ins_diagram.png)

## Xen Communication Facilities

![Xen Component Diagram](xen_diagram.png){#id .class width=150 }

The MwComms isolation channel uses three Xen features to enable communication between the different parts of the system.  First is the grant tables mechanism.  Every Xen domain has a grant table which is a list of shared memory pages and information on what domains have access to those pages. When a domain shares a page of memory with another domain, it is done through a grant reference which is an integer that indexes into the grant table and can be shared with other domains to give them access to the pages.  Next is the Xenstore, a storage space that is shared between domains for the purpose of sharing information.  Domains can place watch points on certain directories that are triggered when data is written to them, this allows for the sharing of grant refs and any other configuration data that may need to be shared with other domains.  The final major Xen feature used is the event channel, which is similar to a hardware interrupt.  An event channel is shared between domains by publishing the event channel port number to Xenstore and is associated with a ring buffer.  The event channel is signaled whenever a message is written to the ring buffer, which allows any domain interacting with the ring buffer to block waiting for messages instead polling.

## Protected Virtual machine

![MWcomms Architecture diagram](pvm_diagram.png){#id .class width=250}

The protected virtual machine is an unprivileged domain running on top of the Xen hypervisor.  The PVM hosts the protected application, the shared library shim as well as the MwComms kernel module which is responsible for coordinating all syscalls, sending them over the Xen ring buffer, and providing the MAGICWAND detection and mitigation engine with telemetry data regarding the state of the protected application's network stack.

### Shim
The purpose of the shim is to transparently bypass the operating system network stack in favor of a direct shared-memory connection to the associated unikernel network stack.  This is done by intercepting the system calls in Table 1.4 and packing them into a MwComms message structure and writing it to the MwComms driver or by calling an ioctl to set socket parameters.  In the case of a system call needing to perform an operation on a local file descriptor or a local socket of a type other than IPv4 the calls are forwarded to the correct libraries for the operating system to handle.

|          |         |          |             |
|:---------|---------|----------|:------------|
| write    | socket  | connect  | getsockopt  |
| read     | bind    | send     | setsockopt  |
| readv    | listen  | sendto   | getsockname |
| writev   | accept  | recv     | getpeername |
| close    | accept4 | recvfrom | fcntl       |
| shutdown |         |          |             |
Table 4.1 list of functions intercepted by shim shared library

### MwComms Kernel Module

The PVM kernel module contains the majority of the logic that maintains the sate of the MwComms network isolation channel. It is responsible for setting up and sharing the grant references that compose the ring buffer, keeps track of and reports all socket information to the NetFlow channel, receives all requests from the shim, keeps track of process information and state, and receives replies from the INS and forwards them to the protected process. 


## Isolated Network Stack

![INS Architecture diagram](rumprun_diagram.png){#id .class width=200 }


### Rumprun Unikernel

The Rumprun unikernel is a minimally modified NetBSD rump kernel that is able to run unmodified POSIX code as a unikernel.  The Rumprun unikernel was chosen over other unikernels to act as the isolated network stack because, as a general purpose unikernel, it has most operating system components already built in, allowing development work to focus on core application infrastructure instead of writing drivers that already exist. The unikernel component of the MwComms channel is where the commands that have been forwarded over the ring buffer are executed and responses returned.  The INS also has a driver component that allows it to interface with the Xen ring buffer.

## Frontend

The frontend component is a python script that watches the Xenstore for available INS's and what ports they have open, and forwards traffic via IP tables.  The frontend scrip is also responsible for spinning up INS's and assigning them 

//TODO what do they assign them?

## Netflow Interface
//TODO what all does the netflow interface do?


Integration
===========

The MwComms isolated network channel was tested to work with the TwoSix container running apache, as well as 

* ran on twosix apache container
* sucessfully ran some attacks on the INS serving 

Results
=======



Performance
===========



References
==========

[1]	A. Madhavapeddy, R. Mortier, C. Rotsos, D. Scott, B. Singh, T. Gazagnaire, S. Smith, S. Hand, and J. Crowcroft, “Unikernels: Library operating systems for the cloud,” in ACM SIGPLAN Notices, 2013, vol. 48, pp. 461–472.


END OF REPORT
=============
