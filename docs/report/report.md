Overview
========

###### Glossary of terms

| Term | Definition                                                                |
|:-----|:--------------------------------------------------------------------------|
| INS  | Isolated Network Stack                                                    |
| PVM  | Protected virtual machine                                                 |
| shim | Shared libary used by LD_PRELOAD to intercept all socket related syscalls |
|      |                                                                           |


Introduction
------------
Methods for Automatic Generalized Instrumentation of Components for Wholesale Application and Network Defense (MAGICWAND), is a project designed to demonstrate a groundbreaking approach to detecting low-volume, distributed denial of service (LVDDoS) attacks delivered as part of a protected operating environment for off-the-shelf applications and services. MAGICWAND has been developed to meet the needs of the Technical Area 3 portion of the extreme Distributed Denial of Service (XD3) Broad Agency Announcement.  MAGICWAND can be split into two main components - the mitigation and detection engine developed by Two Six Labs and the Isolated Network Stack (INS) component developed by StarLab.  The the scope of this document is limited to the INS component of MAGICWAND and will not cover other components of the MAGICWAND system, except when referring to the parts of the system designed to interface with the detection and mitigation system.  

High level overview of the INS
---------------------------------
The purpose of the INS is to provide a level of isolation between an application residing in a protected virtual machine and a potential attacker.  The way this is accomplished is by intercepting all syscalls pertaining to internet sockets and forwarding them over a xen shared memory ring buffer via custom message protocol to a modified rumprun unikernel.  Once the rumprun unikernel recieves the message, it is translated into the relevant syscall and executed.  


Summary of Work
===============

The Star Lab team has successfully implemented and tested the full application agnostic isolated network stack (INS) with multiple applications. This includes using the NetFlow API to monitor application network traffic and running multiplefront end UniKernel VMs to handle large request load.   


This includes the following accomplishments:  

1.  Application agnostic shared library interface to the isolated network stacktested with Apache2 and NGINX web servers.

2. Loaded MwComms linux kernel driver in a Xen VM and communicated with multiplefront end Xen VMs running fully tunable Unikernels.

3. Using the Xen high speed memory channel for transferring data between the protected VM kernel driver and each INS instance.

4. Monitored protected application network traffic using the MwComms driver NetFlowinterface. Successfully sent request/response  messages over NetFlow interface in preparation for handling mitigation commands.

5. Run multiple UniKernel front end Xen VMs to handle network socket requests. EachUniKernel is separately tunable by MwComms using the NetFlow interface.

6. Implemented a front end load balancer used to spin up new INS instances thatdynamically handle network traffic flowing to the protected application. Testedwith 23 INS instances supporting 10,350 simultaneous connections.

7. Designed and implemented protocol for communicating socket call parameters and returnvalues between Xen paravirtualized guests over high speed Xen shared memory ring buffer.

8. Created a custom virtual filesystem to reduce ring buffer congestion and improveperformance.
Previously a message had to traverse the ring buffer, wait through a poll call andreturn again. With the custom VFS, polling is done on custom file objects that get updatedbehind the scenes.

9. Modified Rump UniKernel maximum allowable open files value which originally limited thesystem to have around 200 open sockets to allow over 4000 connections to one INS instance.

10. Limited INS to roughly 30ms per transaction overhead in data throughput tests.


A higher level list of all the work completed by Star Lab as part of the project.
Basically, where we spent our time and effort, maybe highlight unique or impactful
features or designs we implemented. This section could be a bulleted list or a
sub-section for each item and a small paragraph describing each item. We can probably
pull a lot of this information from the MSRs.


Architecture
============

A higher level technical description of our part of the system. Describe each sub-system
(ATP, shim, mwcomms, ring buffer, ins, xen, VMs, etc), APIs, interfaces, interactions, maybe some
technical details, this section should set the stage for the detailed design. Include a diagram
that has all the major pieces listed, entry points and APIs and lines indicating communication
and interaction.

![INS Architecture diagram](./report/ins_diagram.png)

Design
======

A lower level, technical section that describes the details about the design implemented.
Maybe a bulleted list of each major sub-system along with a paragraph or two describing
implementation details including things like programming language, how the sub-system works,
why we chose that solution over other possible solutions, performance notes, special features,
debugging, security, etc. For example the INS, why did we choose Rump, what are the pros and
cons, why didn't we choose a linux OS or containers, etc. Include any interesting implementation
details or diagrams.


Operation
=========

Describe how it works. What is an APT (application to protect), how are we protecting it?
What is a mitigation? How are we impacting performance, provide a diagram or chart and compare
an actual application like Apache with and without the INS/MWCOMMS stack. How might this actually
be deployed and used in the field.


Artifacts / Source Code Layout
==============================

Describe where things are in the Git repository. I think this would be a useful section for
someone picking up this project/code in the future. Maybe a bulleted list of the major code
sub-sections (shim, mwcomms, ins) then give git repository pathnames and filenames. Maybe also
a general overview of how all the pieces share things like data structures, ie the common header
files. This would have been a great thing to have when I started.

Development Environment
=======================

This section should describe in detail how someone would setup a system with our code and get it all
running and how verify it's actually working. I would pull in our "setup" wiki contents at a bare
minimum, then add information about netflow, any scripts such as the netflow library and example
script, the mw_distro_ins.py script for controlling INS instances, debugging built into the code, like
the debugfs or gdb, or shim logging. Point to the Makefiles where we have features commented out. This
should be a very practical section, after reading this someone not familiar with the project could setup
a development environment, verify everything is working as designed, make code changes to each major
sub-system, compile those changes and restart the environment with those changes.

---------------------------------------------------------------------------
