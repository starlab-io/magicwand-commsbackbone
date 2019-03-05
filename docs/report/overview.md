
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
