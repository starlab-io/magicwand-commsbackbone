
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



Introduction
------------
Methods for Automatic Generalized Instrumentation of Components for Wholesale Application and Network Defense (MAGICWAND) is designed to demonstrate a groundbreaking approach to detecting low-volume, distributed denial of service (LVDDoS) attacks delivered as part of a protected operating environment for off-the-shelf applications and services. MAGICWAND was developed to meet the needs of the Technical Area 3 portion of the extreme Distributed Denial of Service (XD3) Broad Agency Announcement.  MAGICWAND can be split into two main components - the mitigation and detection engine developed by Two Six Labs and the Isolated Network Stack (INS) component developed by Star Lab.  The scope of this document is limited to the INS component of MAGICWAND and will not cover other components of the MAGICWAND system, except when referring to the parts of the system designed to interface with the detection and mitigation engine.

Unikernels are an emerging innovation for developing and deploying secure lightweight stacks that contain a minimal set of libraries, services, and code. The Rumprun unikernel is a general purpose unikernel built on top of NetBSD rump kernels that provides high quality componentized drivers, a libc, and tools to build POSIX-like code into a Rumprun unikernel. [1] The researchers at Star Lab chose the Rumprun unikernel because the built in drivers and libc allow development efforts to focus on core functionality instead of writing drivers and infrastructure that already exist while still providing many of the benefits of a unikernel.

The Xen hypervisor is a lightweight and secure virtualization technology that sits between the hardware and the operating system.  It provides strong isolation between virtual machines on the same host as well as provides a high speed shared memory communication channel, event notification channel, and procfs like interface to share data between virtual machines on the same physical host.  Xen is composed of a primary virtual machine domain called Domain 0 or Dom0, all other unprivileged virtual machine hosts are referred to as DomU's.  Domain 0 is the host from which all commands are run to create, start, stop and destroy Xen domains.

At a high-level, the MAGICWAND isolated network channel allows an arbitrary application that may be at risk for LVDDoS attacks to have all of it's network related syscalls intercepted and forwarded to another host where they are run. This system is called the MwComms isolated network channel and is composed of two major components: the protected virtual machine (PVM) and the isolated network stack (INS). The isolated network channel works by intercepting all IPv4 system calls made by the protected application and forwarding those calls over the Xen shared memory communication channel to the INS where the calls are then translated and executed. Running the protected application in this manner allows for finer grained monitoring of its resource consumption, usage, and retention than could be observed otherwise.


[1]	A. Madhavapeddy, R. Mortier, C. Rotsos, D. Scott, B. Singh, T. Gazagnaire, S. Smith, S. Hand, and J. Crowcroft, “Unikernels: Library operating systems for the cloud,” in ACM SIGPLAN Notices, 2013, vol. 48, pp. 461–472.
